/*
 * Copyright (C) 2024 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>

#define SR200_MAGIC 			0xEA
#define SR200_SET_PWR			_IOW(SR200_MAGIC, 0x01, uint32_t)
#define SR200_SET_FWD			_IOW(SR200_MAGIC, 0x02, uint32_t)
#define SR200_ESE_RESET			_IOW(SR200_MAGIC, 0x03, uint32_t)
#define SR200_GET_THROUGHPUT		_IOW(SR200_MAGIC, 0x04, uint32_t)

/* SR200_SET_PWR ioctl's argument */
#define PWR_DISABLE			0
#define PWR_ENABLE			1
#define ABORT_READ_PENDING		2
#define PWR_SUSPEND			3
#define PWR_RESUME			4
#define PWR_KEEP_POWER			5 /* For low-level utils only */

#define REGULATOR_VOLTAGE_MIN_UV	1704000
#define REGULATOR_VOLTAGE_MAX_UV	1800000

#define REGULATOR_LOAD_NPM_UA		19000
#define REGULATOR_LOAD_LPM_UA		1000

#define MAX_PAYLOAD_LEN			4200
#define FW_HEADER_LEN			2
#define UCI_HEADER_LEN			4

/* First byte in SPI Frame to denote read / write operation */
#define BYTE0_OPERATION_WRITE		0x0
#define BYTE0_OPERATION_READ		0xFF

/* pm_wakeup_dev_event delay time after TX */
#define SR200_PM_WAKEUP_DELAY_TX_MS	500

struct sr200_spi_platform_data {
	unsigned int irq_gpio;
	unsigned int reset_gpio;

	struct regulator *regulator_1v8_dig;
	struct regulator *regulator_1v8_rf;
};

typedef enum {
	/* Mode of communication with UCI FW
	 * for ranging, calibration, and other use cases */
	RX_MODE_UCI = 0,
	/* Mode of communication with SR2XX
	 * for FW Update, Life Cycle Management, etc. */
	RX_MODE_HDLL,
} rx_mode_t;

#define NR_RX_BUFFERS			8

struct rx_buffer {
	void *buff;
	size_t len;
	rx_mode_t rx_mode;
};

struct rx_queue {
	int head;
	int tail;
	spinlock_t lock;
	struct rx_buffer buffers[NR_RX_BUFFERS];
	wait_queue_head_t wq;
};

struct sr200_dev {
	struct spi_device *spi;
	struct miscdevice sr200_misc;

	struct regulator *regulator_1v8_dig;
	struct regulator *regulator_1v8_rf;

	unsigned int irq_gpio;
	unsigned int reset_gpio;
	int irq_nr;

	uint8_t *tx_buff;
	struct spi_message rx_fw_header_msg;
	struct spi_transfer rx_fw_header_xfer;

	struct spi_message rx_uci_header_msg;
	struct spi_transfer rx_uci_header_xfer;

	struct spi_message rx_payload_msg;
	struct spi_transfer rx_payload_xfer;

	rx_mode_t rx_mode;
	rx_mode_t cur_rx_mode;

	struct rx_queue rxq;
	struct work_struct rx_work;
	struct completion rx_complete;

	/*
	 * SRFLAGS_READ_QUEUED:
	 * RX IRQ is disabled until the 1st read() called after power on.
	 * So 1st SPI RX should be controlled by upper-layer, not here.
	 *
	 * SR200 starts from HDLL mode and the state is automatically
	 * switched to UCI mode with 50 ms timeout after the first RX
	 * of Reading HDLL ready notification. The upper layer should send
	 * GET_INFO command within 50ms to stays at HDLL mode.
	 */
#define SRFLAGS_POWER_ON	0
#define SRFLAGS_READ_QUEUED	1
#define SRFLAGS_QUEUE_ABORTED	2
#define SRFLAGS_RX_IN_PROGRESS	3
#define SRFLAGS_KEEP_POWER_ON	4	/* for low-level util, keep power on after release */
	volatile unsigned long flags;

	/* XXX: for debugging */
	atomic_t irq_depth;
};

static bool srflags_test(struct sr200_dev *srdev, unsigned long flags)
{
	return test_bit(flags, &srdev->flags);
}

static void srflags_set(struct sr200_dev *srdev, unsigned long flags)
{
	dev_dbg(&srdev->spi->dev, "flag set %ld\n", flags);

	smp_mb__before_atomic();
	set_bit(flags, &srdev->flags);
	smp_mb__after_atomic();
}

static void srflags_clear(struct sr200_dev *srdev, unsigned long flags)
{
	dev_dbg(&srdev->spi->dev, "flag clear %ld\n", flags);

	smp_mb__before_atomic();
	clear_bit(flags, &srdev->flags);
	smp_mb__after_atomic();
}

static bool srflags_test_and_set(struct sr200_dev *srdev, unsigned long flags)
{
	bool ret;

	dev_dbg(&srdev->spi->dev, "flag test-and-set %ld\n", flags);

	smp_mb__before_atomic();
	ret = test_and_set_bit(flags, &srdev->flags);
	smp_mb__after_atomic();
	return ret;
}

static void set_rx_mode(struct sr200_dev *srdev, rx_mode_t rx_mode)
{
	dev_dbg(&srdev->spi->dev, "set mode %s\n",
		rx_mode == RX_MODE_UCI ? "UCI" : "HDLL");
	srdev->rx_mode = rx_mode;
}

static void sr200_enable_irq(struct sr200_dev *srdev)
{
	int depth = atomic_inc_return(&srdev->irq_depth);

	dev_dbg(&srdev->spi->dev, "enable irq, depth=%d\n", depth);
	if (depth == 0) {
		enable_irq(srdev->irq_nr);
	} else if (depth > 0) {
		/* this shouldn't happen */
		dev_warn(&srdev->spi->dev, "enable irq unbalanced, depth=%d\n", depth);
	}
}

/* disable_irq can be nested */
static void sr200_disable_irq(struct sr200_dev *srdev)
{
	int depth = atomic_dec_return(&srdev->irq_depth);

	dev_dbg(&srdev->spi->dev, "disable irq, depth=%d\n", depth);
	if (depth == -1)
		disable_irq_nosync(srdev->irq_nr);
}

/* disable irq only when it's enabled */
static void sr200_disable_irq_unnested(struct sr200_dev *srdev)
{
	int depth = atomic_cmpxchg(&srdev->irq_depth, 0, -1);

	if (depth == 0) {
		dev_dbg(&srdev->spi->dev, "disable irq_unnested\n");
		disable_irq_nosync(srdev->irq_nr);
	} else {
		dev_dbg(&srdev->spi->dev, "disable irq_unnested - skipped, depth=%d.\n", depth);
	}
}

static void rxq_init(struct sr200_dev *srdev)
{
	struct rx_queue *rxq = &srdev->rxq;

	memset(rxq, 0, sizeof(*rxq));
	spin_lock_init(&rxq->lock);
	init_waitqueue_head(&rxq->wq);
}

static void rxq_clear(struct sr200_dev *srdev)
{
	struct rx_queue *rxq = &srdev->rxq;
	int i;

	spin_lock(&rxq->lock);
	for (i = 0; i < NR_RX_BUFFERS; i++) {
		struct rx_buffer *rxbuf = &rxq->buffers[i];
		if (rxbuf->buff) {
			dev_dbg(&srdev->spi->dev,
				"delete rx buffer at slot %d\n", i);
			kfree(rxbuf->buff);
			rxbuf->buff = NULL;
			rxbuf->len = 0;
		}
	}
	rxq->head = 0;
	rxq->tail = 0;
	spin_unlock(&rxq->lock);
}

static bool rxq_is_empty(struct sr200_dev *srdev)
{
	bool ret;

	spin_lock(&srdev->rxq.lock);
	ret = (srdev->rxq.head == srdev->rxq.tail);
	spin_unlock(&srdev->rxq.lock);

	return ret;
}

static void rxq_abort(struct sr200_dev *srdev)
{
	struct rx_queue *rxq = &srdev->rxq;

	/* Stop all waiters */
	srflags_set(srdev, SRFLAGS_QUEUE_ABORTED);
	wake_up_all(&rxq->wq);
}

static int rxq_pop(struct sr200_dev *srdev, struct rx_buffer *rx_buff)
{
	struct rx_queue *rxq = &srdev->rxq;
	int ret;

	for (;;) {
		if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED))
			return -ESHUTDOWN;

		spin_lock(&rxq->lock);
		if (rxq->head == rxq->tail) {
			/* Empty */
			spin_unlock(&rxq->lock);
			dev_dbg(&srdev->spi->dev,
				"rx ring empty, wait for rx event.\n");
			ret = wait_event_interruptible(rxq->wq, rxq->head != rxq->tail ||
						srflags_test(srdev, SRFLAGS_QUEUE_ABORTED));
			if (ret)
				return ret;
			if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
				dev_info(&srdev->spi->dev, "read() aborted by read-abort.\n");
				return -ESHUTDOWN;
			}
		} else {
			dev_dbg(&srdev->spi->dev,
				"Dequeue rx ring slot %d, %zu bytes\n",
				rxq->tail, rxq->buffers[rxq->tail].len);
			memcpy(rx_buff, &rxq->buffers[rxq->tail], sizeof(*rx_buff));
			rxq->buffers[rxq->tail].buff = NULL;
			rxq->buffers[rxq->tail].len = 0;
			rxq->tail = (rxq->tail + 1) % NR_RX_BUFFERS;
			spin_unlock(&rxq->lock);
			wake_up_all(&rxq->wq);
			break;
		}
	}
	return 0;
}

static int rxq_push(struct sr200_dev *srdev, void *buff, size_t len, rx_mode_t rx_mode)
{
	struct rx_queue *rxq = &srdev->rxq;
	int ret, next;

	for (;;) {
		if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
			dev_warn(&srdev->spi->dev,
				 "packet received while aborted\n");
			return -ESHUTDOWN;
		}

		spin_lock(&rxq->lock);
		next = (rxq->head + 1) % NR_RX_BUFFERS;
		if (next == rxq->tail) {
			/* Full */
			spin_unlock(&rxq->lock);
			dev_warn(&srdev->spi->dev, "RX queue full\n");
			ret = wait_event_interruptible(rxq->wq, next != rxq->tail ||
						srflags_test(srdev, SRFLAGS_QUEUE_ABORTED));
			if (ret) {
				dev_err(&srdev->spi->dev,
					"Failed to queue rx buffer, drop a packet.\n");
				return ret;
			}
			if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
				dev_info(&srdev->spi->dev, "write() returned by read-abort.\n");
				return -ESHUTDOWN;
			}
		} else {
			struct rx_buffer *rx_buff = &rxq->buffers[rxq->head];
			dev_dbg(&srdev->spi->dev,
				"Queue a packet to slot %d (%zu bytes)\n",
				rxq->head, len);
			rx_buff->buff = buff;
			rx_buff->len = len;
			rx_buff->rx_mode = rx_mode;
			rxq->head = next;
			spin_unlock(&rxq->lock);
			wake_up_all(&rxq->wq);
			break;
		}
	}
	return 0;
}

/* disable RX interrupt + wait for ongoing RX finished */
static void rx_pause(struct sr200_dev *srdev)
{
	long timeout = msecs_to_jiffies(200);
	int retries = 3;

	pm_stay_awake(&srdev->spi->dev);
	sr200_disable_irq(srdev);

	while (retries--) {
		if (srflags_test(srdev, SRFLAGS_RX_IN_PROGRESS)) {
			dev_dbg(&srdev->spi->dev, "rx_pause: rx in progress, wait for rx complete\n");
			wait_for_completion_timeout(&srdev->rx_complete, timeout);
		} else {
			dev_dbg(&srdev->spi->dev, "rx_pause: rx completed\n");
			break;
		}
	}

	if (retries < 0) {
		dev_err(&srdev->spi->dev, "rx pause timedout!\n");
	}
}

static void rx_resume(struct sr200_dev *srdev)
{
	dev_dbg(&srdev->spi->dev, "rx_resume\n");
	sr200_enable_irq(srdev);
	pm_relax(&srdev->spi->dev);
}

static int sr200_power_ctl(struct sr200_dev *srdev, bool on)
{
	int ret = 0;

	if (on == srflags_test(srdev, SRFLAGS_POWER_ON)) {
		dev_warn(&srdev->spi->dev, "chip already %s.\n",
			 on ? "enabled" : "disabled");
		return 0;
	}

	dev_info(&srdev->spi->dev, "chip %s requested.\n",
		 on ? "enabled" : "disabled");

	if (on) {
		ret = regulator_set_load(srdev->regulator_1v8_dig, REGULATOR_LOAD_NPM_UA);
		if (ret) {
			dev_err(&srdev->spi->dev,
				"failed to set dig regulator load\n");
			return ret;
		}
		ret = regulator_set_load(srdev->regulator_1v8_rf, REGULATOR_LOAD_NPM_UA);
		if (ret) {
			dev_err(&srdev->spi->dev,
				"failed to set rf regulator load\n");
			return ret;
		}
		usleep_range(50, 100);

		srflags_set(srdev, SRFLAGS_POWER_ON);
		gpio_set_value(srdev->reset_gpio, 1);
		msleep(10);
	} else if (!on) {
		gpio_set_value(srdev->reset_gpio, 0);
		msleep(10);

		ret = regulator_set_load(srdev->regulator_1v8_dig, REGULATOR_LOAD_LPM_UA);
		if (ret) {
			dev_err(&srdev->spi->dev,
				"failed to set dig regulator load\n");
			return ret;
		}
		ret = regulator_set_load(srdev->regulator_1v8_rf, REGULATOR_LOAD_LPM_UA);
		if (ret) {
			dev_err(&srdev->spi->dev,
				"failed to set rf regulator load\n");
			return ret;
		}

		srflags_clear(srdev, SRFLAGS_POWER_ON);
	}
	dev_info(&srdev->spi->dev, "chip %s.\n", on ? "enabled" : "disabled");

	return ret;
}

static long sr200_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct sr200_dev *srdev = container_of(filp->private_data, struct sr200_dev, sr200_misc);
	if (srdev == NULL)
		return -ENODEV;

	dev_dbg(&srdev->spi->dev, "ioctl cmd=0x%x arg=0x%lx\n", cmd, arg);

	switch (cmd) {
	case SR200_SET_PWR:
		if (arg == PWR_ENABLE) {
			/* rx should be re-enabled on the first read call */
			srflags_clear(srdev, SRFLAGS_READ_QUEUED);
			srflags_clear(srdev, SRFLAGS_QUEUE_ABORTED);
			sr200_disable_irq_unnested(srdev);
			ret = sr200_power_ctl(srdev, true);
			if (ret)
				return ret;
		} else if (arg == PWR_DISABLE) {
			rxq_abort(srdev);

			rx_pause(srdev);
			rxq_clear(srdev);
			ret = sr200_power_ctl(srdev, false);

			/* okay to resume as there will be no rx irqs after power off */
			rx_resume(srdev);
		} else if (arg == ABORT_READ_PENDING) {
			dev_info(&srdev->spi->dev, "Abort read pending\n");
			rxq_abort(srdev);
		} else if (arg == PWR_KEEP_POWER) {
			dev_info(&srdev->spi->dev, "Chip won't be powered off.\n");
			srflags_set(srdev, SRFLAGS_KEEP_POWER_ON);
		}
		break;
	case SR200_SET_FWD:
		if (arg == 1) {
			set_rx_mode(srdev, RX_MODE_HDLL);
			dev_info(&srdev->spi->dev, "FW download enabled.\n");
		} else if (arg == 0) {
			/* XXX: clear queued HDLL mode packets */
			rx_pause(srdev);
			rxq_clear(srdev);
			set_rx_mode(srdev, RX_MODE_UCI);
			rx_resume(srdev);

			dev_info(&srdev->spi->dev, "FW download disabled.\n");
		}
		break;
	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

#if defined(CONFIG_COMPAT)
static long sr200_ioctl_compat(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return sr200_ioctl(filp, cmd, arg);
}
#else
#define sr200_ioctl_compat NULL
#endif

/*
 * SR200 rx transaction is consists of 4 context switches.
 *
 *  1) IRQ: spi_async(header)
 *  2) spi-callback: spi_async(payload)
 *  3) spi-callback: schedule_work(header + payload)
 *  4) worker: allocate & queue a packet
 *
 *  - IRQ is disabled through 1) ~ 4)
 *  - Host should not initiate tx while rx transaction is ongoing.
 *
 *  Each side should call "barrier" functions:
 *  - rx: begin_rx_transaction() ~ end_rx_transaction()
 *  - tx: rx_pause() ~ rx_resume()
 */
static void handle_spi_error(struct sr200_dev *srdev)
{
	dev_err(&srdev->spi->dev, "critical error, reset the chip.\n");
	sr200_power_ctl(srdev, false);
	sr200_power_ctl(srdev, true);
}

static void begin_rx_transaction(struct sr200_dev *srdev)
{
	pm_stay_awake(&srdev->spi->dev);
	sr200_disable_irq(srdev);
	reinit_completion(&srdev->rx_complete);
	srflags_set(srdev, SRFLAGS_RX_IN_PROGRESS);
}

static void end_rx_transaction(struct sr200_dev *srdev)
{
	srflags_clear(srdev, SRFLAGS_RX_IN_PROGRESS);
	complete_all(&srdev->rx_complete);
	sr200_enable_irq(srdev);
	pm_relax(&srdev->spi->dev);
}

static void rx_payload_work_handler(struct work_struct *work)
{
	struct sr200_dev *srdev = container_of(work, struct sr200_dev, rx_work);
	void *buf;
	size_t header_len, payload_len, buf_len;

	if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
		end_rx_transaction(srdev);
		return;
	}

	if (srdev->cur_rx_mode == RX_MODE_HDLL) {
		header_len = srdev->rx_fw_header_xfer.len - 1;
	} else {
		header_len = srdev->rx_uci_header_xfer.len - 1;
	}
	payload_len = srdev->rx_payload_xfer.len - 1;
	buf_len = header_len + payload_len;

	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		dev_err(&srdev->spi->dev,
			"Failed to allocate payload buffer %zu bytes\n", buf_len);
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
		return;
	}

	if (srdev->cur_rx_mode == RX_MODE_HDLL) {
		memcpy(buf, srdev->rx_fw_header_xfer.rx_buf + 1, header_len);
	} else {
		memcpy(buf, srdev->rx_uci_header_xfer.rx_buf + 1, header_len);
	}
	memcpy(buf + header_len, srdev->rx_payload_xfer.rx_buf + 1, payload_len);

	rxq_push(srdev, buf, buf_len, srdev->cur_rx_mode);

	end_rx_transaction(srdev);
}

static void rx_complete_payload(void *context)
{
	struct sr200_dev *srdev = context;

	if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
		end_rx_transaction(srdev);
		return;
	}

	dev_dbg(&srdev->spi->dev, "payload received\n");
	if (srdev->rx_payload_msg.status) {
		dev_err(&srdev->spi->dev, "Error in payload rx, status=%d\n",
			srdev->rx_payload_msg.status);
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
		return;
	}
	schedule_work(&srdev->rx_work);
}

static void rx_complete_fw_header(void *context)
{
	struct sr200_dev *srdev = context;
	uint8_t *header_rx_buf;
	unsigned int payload_len, packet_len;
	int ret;

	if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
		end_rx_transaction(srdev);
		return;
	}

	if (srdev->rx_fw_header_msg.status) {
		dev_err(&srdev->spi->dev, "Error in hdll header rx, status=%d\n",
			srdev->rx_fw_header_msg.status);
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
		return;
	}

	header_rx_buf = srdev->rx_fw_header_xfer.rx_buf;
	payload_len = ((header_rx_buf[1] & 0x1f) << 8) | header_rx_buf[2];
	// additional 3 bytes are for DIR(1) + CRC16(2)
	packet_len = payload_len + 3;

	if (packet_len > MAX_PAYLOAD_LEN) {
		dev_err(&srdev->spi->dev,
			"Buffer overflow for hdll payload %u bytes\n", payload_len);
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
		return;
	}

	srdev->rx_payload_xfer.len = packet_len;

	dev_dbg(&srdev->spi->dev,
		"FW header received, queue payload %u + 3 bytes\n",
		payload_len);
	ret = spi_async(srdev->spi, &srdev->rx_payload_msg);
	if (ret) {
		dev_err(&srdev->spi->dev, "Failed to queue hdll payload rx\n");
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
	}
}

static void rx_complete_uci_header(void *context)
{
	struct sr200_dev *srdev = context;
	uint8_t *uci_header;
	unsigned int payload_len, packet_len;
	int ret;

	if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
		end_rx_transaction(srdev);
		return;
	}

	if (srdev->rx_uci_header_msg.status) {
		dev_err(&srdev->spi->dev, "Error in uci header rx, status=%d\n",
			srdev->rx_uci_header_msg.status);
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
		return;
	}

	uci_header = srdev->rx_uci_header_xfer.rx_buf + 1;
	if ((uci_header[0] & 0xE0) == 0) {
		/* Data packet : Extended Payload Length */
		payload_len = (uci_header[3] << 8) | uci_header[2];
	} else {
		/* CMD/RSP/NTF */
		if (uci_header[1] & 0x80) {	/* Extented Payload Length flag */
			payload_len = (uci_header[3] << 8) | uci_header[2];
		} else {
			payload_len = uci_header[3];
		}
	}
	// additional 1 byte is for DIR(1)
	packet_len = payload_len + 1;

	if (packet_len > MAX_PAYLOAD_LEN) {
		dev_err(&srdev->spi->dev,
			"Buffer overflow for uci payload %u bytes\n", payload_len);
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
		return;
	}

	dev_dbg(&srdev->spi->dev,
		"UCI header received, queue payload rx %u bytes\n", packet_len);
	srdev->rx_payload_xfer.len = packet_len;
	ret = spi_async(srdev->spi, &srdev->rx_payload_msg);
	if (ret) {
		dev_err(&srdev->spi->dev, "Failed to queue uci payload rx\n");
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
	}
}

static irqreturn_t sr200_irq_handler(int irq, void *dev_id)
{
	struct sr200_dev *srdev = dev_id;
	int ret;

	if (!srdev)
		return IRQ_NONE;

	if (!srflags_test(srdev, SRFLAGS_POWER_ON)) {
		dev_err(&srdev->spi->dev, "rx irq while powered down.\n");
		return IRQ_NONE;
	}

	if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
		dev_err(&srdev->spi->dev, "rx irq after read aborted.\n");
		return IRQ_NONE;
	}

	begin_rx_transaction(srdev);

	srdev->cur_rx_mode = srdev->rx_mode;
	if (srdev->cur_rx_mode == RX_MODE_HDLL) {
		dev_dbg(&srdev->spi->dev, "queue fw header rx\n");
		ret = spi_async(srdev->spi, &srdev->rx_fw_header_msg);
	} else {
		dev_dbg(&srdev->spi->dev, "queue uci header rx\n");
		ret = spi_async(srdev->spi, &srdev->rx_uci_header_msg);
	}

	if (ret) {
		dev_err(&srdev->spi->dev, "Failed to queue header rx\n");
		handle_spi_error(srdev);
		end_rx_transaction(srdev);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int sr200_init_spi_transfers(struct sr200_dev *srdev)
{
	struct device *dev = &srdev->spi->dev;
	uint8_t *rx_buf, *tx_buf;

	srdev->tx_buff = devm_kmalloc(dev, MAX_PAYLOAD_LEN, GFP_KERNEL);
	srdev->tx_buff[0] = BYTE0_OPERATION_WRITE;

	/* RX HEADER - FW */
	spi_message_init(&srdev->rx_fw_header_msg);
	srdev->rx_fw_header_msg.context = srdev;
	srdev->rx_fw_header_msg.complete = rx_complete_fw_header;

	rx_buf = devm_kzalloc(dev, FW_HEADER_LEN + 1, GFP_KERNEL);
	tx_buf = devm_kzalloc(dev, FW_HEADER_LEN + 1, GFP_KERNEL);

	if (!rx_buf || !tx_buf)
		return -ENOMEM;

	tx_buf[0] = BYTE0_OPERATION_READ;
	srdev->rx_fw_header_xfer.tx_buf = tx_buf;
	srdev->rx_fw_header_xfer.rx_buf = rx_buf;
	srdev->rx_fw_header_xfer.len = FW_HEADER_LEN + 1;

	spi_message_add_tail(&srdev->rx_fw_header_xfer,
			     &srdev->rx_fw_header_msg);

	/* RX HEADER - UCI */
	spi_message_init(&srdev->rx_uci_header_msg);
	srdev->rx_uci_header_msg.context = srdev;
	srdev->rx_uci_header_msg.complete = rx_complete_uci_header;

	rx_buf = devm_kzalloc(dev, UCI_HEADER_LEN + 1, GFP_KERNEL);
	tx_buf = devm_kzalloc(dev, UCI_HEADER_LEN + 1, GFP_KERNEL);

	if (!rx_buf || !tx_buf)
		return -ENOMEM;

	tx_buf[0] = BYTE0_OPERATION_READ;
	srdev->rx_uci_header_xfer.tx_buf = tx_buf;
	srdev->rx_uci_header_xfer.rx_buf = rx_buf;
	srdev->rx_uci_header_xfer.len = UCI_HEADER_LEN + 1;

	spi_message_add_tail(&srdev->rx_uci_header_xfer,
			     &srdev->rx_uci_header_msg);

	/* RX Payload */
	spi_message_init(&srdev->rx_payload_msg);
	srdev->rx_payload_msg.context = srdev;
	srdev->rx_payload_msg.complete = rx_complete_payload;

	tx_buf = devm_kzalloc(dev, MAX_PAYLOAD_LEN, GFP_KERNEL);
	rx_buf = devm_kzalloc(dev, MAX_PAYLOAD_LEN, GFP_KERNEL);

	if (!tx_buf || !rx_buf)
		return -ENOMEM;

	tx_buf[0] = BYTE0_OPERATION_READ;
	srdev->rx_payload_xfer.tx_buf = tx_buf;
	srdev->rx_payload_xfer.rx_buf = rx_buf;
	srdev->rx_payload_xfer.len = 0;
	spi_message_add_tail(&srdev->rx_payload_xfer, &srdev->rx_payload_msg);

	INIT_WORK(&srdev->rx_work, rx_payload_work_handler);

	rxq_init(srdev);

	return 0;
}

static ssize_t sr200_write(struct file *filp, const char *buf, size_t count, loff_t *offset)
{
	int ret;
	struct sr200_dev *srdev = container_of(filp->private_data, struct sr200_dev, sr200_misc);

	if (!srflags_test(srdev, SRFLAGS_POWER_ON)) {
		dev_err(&srdev->spi->dev, "write called while powered down.\n");
		return 0;
	}

	if ((count + 1) > MAX_PAYLOAD_LEN) {
		dev_err(&srdev->spi->dev, "write size exceeds.\n");
		return -ENOBUFS;
	}

	dev_dbg(&srdev->spi->dev, "write requested %zu bytes\n", count);

	if (copy_from_user(srdev->tx_buff + 1, buf, count)) {
		dev_err(&srdev->spi->dev, "copy_from_user failed.\n");
		return -EFAULT;
	}
	srdev->tx_buff[0] = BYTE0_OPERATION_WRITE;	// Direction = 0(out)

	rx_pause(srdev);

	ret = spi_write(srdev->spi, srdev->tx_buff, count + 1);

	rx_resume(srdev);

	/* higher chance of incoming RX packet after this TX */
	pm_wakeup_dev_event(&srdev->spi->dev, SR200_PM_WAKEUP_DELAY_TX_MS, true);

	if (ret < 0) {
		dev_err(&srdev->spi->dev, "spi_write failed.\n");
		return ret;
	}

	return count;
}

static ssize_t sr200_read(struct file *filp, char *buf, size_t count, loff_t *offset)
{
	struct sr200_dev *srdev = container_of(filp->private_data, struct sr200_dev, sr200_misc);
	int ret;
	struct rx_buffer rxbuf;
	size_t buf_len;
	unsigned long copied;

	dev_dbg(&srdev->spi->dev, "read requested %zu bytes\n", count);

	if (!srflags_test(srdev, SRFLAGS_POWER_ON)) {
		dev_err(&srdev->spi->dev, "read called while powered down.\n");
		return -ESHUTDOWN;
	}

	if (srflags_test(srdev, SRFLAGS_QUEUE_ABORTED)) {
		dev_err(&srdev->spi->dev, "read called after read aborted.\n");
		return -ESHUTDOWN;
	}

	/* enable rx irq from first read() call after power on */
	if (!srflags_test_and_set(srdev, SRFLAGS_READ_QUEUED)) {
		sr200_enable_irq(srdev);
	}

	if ((filp->f_flags & O_NONBLOCK) && rxq_is_empty(srdev)) {
		return -EAGAIN;
	}

	ret = rxq_pop(srdev, &rxbuf);
	if (ret) {
		if (ret != -ERESTARTSYS)
			dev_err(&srdev->spi->dev, "rx queue pop error %d\n", ret);
		return ret;
	}

	buf_len = rxbuf.len;
	if (buf_len > count) {
		dev_err(&srdev->spi->dev,
			"read user buffer is not enough, trim it to the requested buffer size, "
			"read=%zu, requested=%zu\n",
			buf_len, count);
		buf_len = count;
	}
	copied = copy_to_user(buf, rxbuf.buff, buf_len);
	kfree(rxbuf.buff);
	rxbuf.len = 0;

	if (copied) {
		dev_err(&srdev->spi->dev, "copy_to_user failed %lu\n", copied);
		return copied;
	} else {
		dev_dbg(&srdev->spi->dev, "read returns %zu bytes\n", buf_len);
		return buf_len;
	}
}

static int sr200_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int sr200_release(struct inode *inode, struct file *filp)
{
	struct sr200_dev *srdev = container_of(filp->private_data, struct sr200_dev, sr200_misc);

	rx_pause(srdev);
	rxq_clear(srdev);
	rx_resume(srdev);

	/* upper-layer should have called 'power-off' ioctl before here */
	if (srflags_test(srdev, SRFLAGS_POWER_ON)) {
		if (srflags_test(srdev, SRFLAGS_KEEP_POWER_ON)) {
			dev_info(&srdev->spi->dev, "kernel driver released with power on.\n");
		} else {
			dev_warn(&srdev->spi->dev, "kernel driver released with power on, turn off the chip.\n");
			sr200_power_ctl(srdev, false);
		}
	}

	return 0;
}

static int sr200_hw_init(struct sr200_dev *srdev)
{
	int ret;
	struct device *dev = &srdev->spi->dev;
	struct device_node *np = dev->of_node;

	srdev->irq_gpio = of_get_named_gpio(np, "nxp,sr200-irq", 0);
	if (!gpio_is_valid(srdev->irq_gpio)) {
		dev_err(dev, "Failed to get sr200-irq gpio\n");
		return -EINVAL;
	}

	srdev->reset_gpio = of_get_named_gpio(np, "nxp,sr200-reset", 0);
	if (!gpio_is_valid(srdev->reset_gpio)) {
		dev_err(dev, "Failed to get sr200-reset gpio\n");
		return -EINVAL;
	}

	srdev->regulator_1v8_dig = devm_regulator_get(dev, "nxp,sr200-dig");
	if (IS_ERR(srdev->regulator_1v8_dig)) {
		dev_err(dev, "Failed to get sr200-dig supply\n");
		return -EINVAL;
	}

	srdev->regulator_1v8_rf = devm_regulator_get(dev, "nxp,sr200-rf");
	if (IS_ERR(srdev->regulator_1v8_rf)) {
		dev_err(dev, "Failed to get sr200-rf supply\n");
		return -EINVAL;
	}

	ret = devm_gpio_request(dev, srdev->irq_gpio, "sr200-irq");
	if (ret < 0) {
		dev_err(dev, "gpio request failed irq = %u\n", srdev->irq_gpio);
		return ret;
	}
	ret = devm_gpio_request(dev, srdev->reset_gpio, "sr200-reset");
	if (ret < 0) {
		dev_err(dev, "gpio request failed reset = %u\n",
			srdev->reset_gpio);
		return ret;
	}

	ret = gpio_direction_input(srdev->irq_gpio);
	if (ret < 0)
		return ret;

	ret = gpio_direction_output(srdev->reset_gpio, 0);
	if (ret < 0)
		return ret;

	/* Regulators: start with LPM + RST */
	gpio_set_value(srdev->reset_gpio, 0);

	ret = regulator_set_load(srdev->regulator_1v8_dig, REGULATOR_LOAD_LPM_UA);
	if (ret < 0) {
		dev_err(dev, "Failed to set load DIG %uuA ret=%d\n",
			REGULATOR_LOAD_LPM_UA, ret);
		return ret;
	}
	ret = regulator_set_voltage(srdev->regulator_1v8_dig,
				    REGULATOR_VOLTAGE_MIN_UV,
				    REGULATOR_VOLTAGE_MAX_UV);
	if (ret) {
		dev_err(dev, "Failed to set voltage for DIG ret=%d\n", ret);
		return ret;
	}
	ret = regulator_set_load(srdev->regulator_1v8_rf, REGULATOR_LOAD_LPM_UA);
	if (ret < 0) {
		dev_err(dev, "Failed to set load RF %uuA ret=%d\n",
			REGULATOR_LOAD_LPM_UA, ret);
		return ret;
	}
	ret = regulator_set_voltage(srdev->regulator_1v8_rf,
				    REGULATOR_VOLTAGE_MIN_UV,
				    REGULATOR_VOLTAGE_MAX_UV);
	if (ret) {
		dev_err(dev, "Failed to set voltage for RF ret=%d\n", ret);
		return ret;
	}
	ret = regulator_enable(srdev->regulator_1v8_dig);
	if (ret) {
		dev_err(dev, "Failed to enable DIG regulator\n");
		return ret;
	}
	ret = regulator_enable(srdev->regulator_1v8_rf);
	if (ret) {
		dev_err(dev, "Failed to enable RF regulator\n");
		return ret;
	}

	return 0;
}

static void sr200_hw_deinit(struct sr200_dev *srdev)
{
	regulator_disable(srdev->regulator_1v8_rf);
	regulator_disable(srdev->regulator_1v8_dig);
}

static const struct file_operations sr200_misc_fops = {
	.owner = THIS_MODULE,
	.open = sr200_open,
	.release = sr200_release,
	.unlocked_ioctl = sr200_ioctl,
	.compat_ioctl = sr200_ioctl_compat,
	.read = sr200_read,
	.write = sr200_write,
};

static int sr200_probe(struct spi_device *spi)
{
	int ret;
	struct sr200_dev *srdev = NULL;

	srdev = devm_kzalloc(&spi->dev, sizeof(*srdev), GFP_KERNEL);
	if (!srdev)
		return -ENOMEM;

	srdev->spi = spi;
	dev_set_drvdata(&spi->dev, srdev);

	ret = sr200_hw_init(srdev);
	if (ret < 0)
		return ret;

	ret = sr200_init_spi_transfers(srdev);
	if (ret < 0)
		goto err_probe;

	srdev->sr200_misc.minor = MISC_DYNAMIC_MINOR;
	srdev->sr200_misc.name = "srxxx";
	srdev->sr200_misc.fops = &sr200_misc_fops;
	srdev->sr200_misc.parent = &spi->dev;
	ret = misc_register(&srdev->sr200_misc);
	if (ret < 0) {
		dev_err(&spi->dev, "misc_register failed! %d\n", ret);
		goto err_probe;
	}

	srdev->irq_nr = gpio_to_irq(srdev->irq_gpio);
	if (srdev->irq_nr < 0) {
		dev_err(&spi->dev, "gpio_to_irq failed gpo=%u, ret=%d\n",
			srdev->irq_gpio, srdev->irq_nr);
		goto err_misc;
	}

	init_completion(&srdev->rx_complete);
	atomic_set(&srdev->irq_depth, 0);

	ret = devm_request_irq(&spi->dev, srdev->irq_nr, sr200_irq_handler,
			       IRQ_TYPE_LEVEL_HIGH, srdev->sr200_misc.name,
			       srdev);
	if (ret) {
		dev_err(&spi->dev, "request_irq failed\n");
		goto err_misc;
	}

	device_init_wakeup(&spi->dev, true);

	dev_info(&spi->dev, "sr200 device probed.\n");
	return 0;

err_misc:
	misc_deregister(&srdev->sr200_misc);

err_probe:
	sr200_hw_deinit(srdev);
	return ret;
}

static void sr200_remove(struct spi_device *spi)
{
	struct sr200_dev *srdev = dev_get_drvdata(&spi->dev);

	rx_pause(srdev);
	sr200_power_ctl(srdev, false);
	rx_resume(srdev);

	sr200_hw_deinit(srdev);
	misc_deregister(&srdev->sr200_misc);

	dev_info(&spi->dev, "sr200 device removed.\n");
}

static int sr200_suspend(struct device *dev)
{
	struct sr200_dev *srdev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(srdev->irq_nr);

		/* Doesn't take care of internal irq depth */
		disable_irq(srdev->irq_nr);
		dev_dbg(&srdev->spi->dev, "suspend.\n");
	}
	return 0;
}

static int sr200_resume(struct device *dev)
{
	struct sr200_dev *srdev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		dev_dbg(&srdev->spi->dev, "resume.\n");

		disable_irq_wake(srdev->irq_nr);

		/* Doesn't take care of internal irq depth */
		enable_irq(srdev->irq_nr);
	}

	return 0;
}


static const struct dev_pm_ops sr200_dev_pm_ops =
	{ SET_SYSTEM_SLEEP_PM_OPS(sr200_suspend, sr200_resume)
};

static struct of_device_id sr200_dt_match[] = {
	{
	 .compatible = "nxp,srxxx",
	},
	{ }
};

static struct spi_driver sr200_driver = {
	.driver = {
		   .name = "srxxx",
		   .bus = &spi_bus_type,
		   .owner = THIS_MODULE,
		   .of_match_table = sr200_dt_match,
		   .pm = &sr200_dev_pm_ops,
	},
	.probe = sr200_probe,
	.remove = sr200_remove,
};

static int __init sr200_module_init(void)
{
	return spi_register_driver(&sr200_driver);
}

module_init(sr200_module_init);

static void __exit sr200_module_exit(void)
{
	spi_unregister_driver(&sr200_driver);
}

module_exit(sr200_module_exit);

MODULE_AUTHOR("Ikjoon Jang <ikjn@google.com>");
MODULE_DESCRIPTION("NXP SR200 SPI driver");
MODULE_LICENSE("GPL");
