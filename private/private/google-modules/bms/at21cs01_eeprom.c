/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Google LLC */

/*
 * Driver for Microchip AT21CS01 1-kbit EEPROM.
 * https://www.microchip.com/en-us/product/at21cs01
 *
 * This driver is defined from the details in datasheet:
 * DS20005857G
 * "Single-Wire, I/O Powered 1-Kbit (128 x 8) Serial EEPROM
 * with a Unique, Factory-Programmed 64-Bit Serial Number"
 * from 2023.
 *
 * The AT21CS01 is electrically connected to the circuit via two wires SI/O and
 * GND. The AT21CS01 harvests energy via the SI/O wire and can be discharged of
 * its power by holding the wire low for tDSCHG period of time. The datasheet
 * calls for a pull-up resistor which returns the line level high when neither
 * the host nor AT21CS01 is asserting the line. This ensures continuous power
 * for the AT21CS01.
 *
 * Communication between the AT21CS01 and host is a choreographed sequence of
 * timed exchange between either the host or AT21CS01 asserting the wire low
 * while the other party monitors the timing of the wire being asserted.
 *
 * The communication is a proprietary protocol and the datasheet discusses the
 * particular exchanges for each of the available opcodes used to communciate
 * with the AT21CS01.
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/*
 * The AT21CS01 has fixed manufacturer identification.
 * This is defined in table 8-2 of the datasheet.
 */
#define AT21CS01_MFG_ID 0x00D200

/* Reset low time, device in active state. */
#define T_RESET_US 95

/* Reset recovery time. */
#define T_RRT_US 8

/* Discovery response request. */
#define T_DRR_US 1

/* Host strobe discovery response time. */
#define T_MSDR_US 2

/* Discovery response acknowledge time. */
#define T_DACK_US 8

/* Assert the GPIO which is physically active low for AT21CS01. */
#define GPIO_ASSERTED 1

/* Release the GPIO putting it into high impedance. */
#define GPIO_RELEASED 0

/* Operation code for Read Manufacturer identification. */
#define AT21CS01_OPCODE_MFG_ID 0xC

/* Operation code for setting / getting standard speed mode. */
#define AT21CS01_OPCODE_STD_SPEED   0xD

/* Operation code for setting / getting high speed mode. */
#define AT21CS01_OPCODE_HIGH_SPEED  0xE

enum data_speed {
	DATA_SPEED_HIGH,
	DATA_SPEED_STD,
	DATA_SPEED_NUM_SPEEDS,
};

enum rw_cmd {
	SPEED_WRITE,
	SPEED_READ,
};

struct timing_range_us {
	uint32_t min;
	uint32_t max;
};

const struct data_comm_timing {
	/* Bit frame duration */
	struct timing_range_us t_bit_us;
	/* SI/O high time for start / stop condition */
	struct timing_range_us t_htss_us;
	/* SI/O low time, logic '0' condition */
	struct timing_range_us t_low0_us;
	/* SI/O low time, logic '1' condition */
	struct timing_range_us t_low1_us;
	/* Host SI/O low time during read */
	struct timing_range_us t_rd_us;
	/* Host read strobe time */
	struct timing_range_us t_mrs_us;
	/* Data output hold time (logic '0') */
	struct timing_range_us t_hld0_us;
	/* Client recovery time */
	struct timing_range_us t_rcv_us;
} data_comm_timings[DATA_SPEED_NUM_SPEEDS] = {
	[DATA_SPEED_HIGH] = {
		.t_bit_us  = {11, 25},
		.t_htss_us = {150, 300},
		.t_low0_us = {6, 16},
		.t_low1_us = {1, 2},
		.t_rd_us   = {1, 2},
		.t_mrs_us  = {1, 2},
		.t_hld0_us = {2, 6},
		.t_rcv_us  = {2, 20},
	},
	[DATA_SPEED_STD] = {
		.t_bit_us  = {40, 100},
		.t_htss_us = {600, 1200},
		.t_low0_us = {24, 64},
		.t_low1_us = {4, 8},
		.t_rd_us   = {4, 8},
		.t_mrs_us  = {5, 8},
		.t_hld0_us = {8, 24},
		.t_rcv_us  = {8, 80},
	},
};

struct at21cs01_eeprom {
	struct device *dev;
	struct gpio_desc *sio;
	struct mutex lock;
	uint8_t client_addr;
	const struct data_comm_timing *timing;
};

static DEFINE_SPINLOCK(gpio_bitbang_lock);

/*
 * Receive a bit from the AT21CS01 EEPROM device.
 *
 * The host asserts the line and then the EEPROM can choose to either
 * additionally assert the line or do nothing. The host then releases the line
 * and if the line is kept asserted then the host assumes the EEPROM intends to
 * transmit a logical '0'. The host should assume a logical '1' otherwise.
 */
static void rx_bit(struct at21cs01_eeprom *eeprom, uint8_t *bit)
{
	uint8_t asserted;
	struct gpio_desc *sio = eeprom->sio;

	/* Assert to the AT21CS01 intention to start reading a bit. */
	gpiod_set_value(sio, GPIO_ASSERTED);
	udelay(eeprom->timing->t_rd_us.min);
	/* Release the line allowing for EEPROM control state. */
	gpiod_set_value(sio, GPIO_RELEASED);

	/* Determine if the EEPROM is now independently asserting the line. */
	asserted = gpiod_get_value(sio);
	*bit = (asserted == GPIO_ASSERTED) ? 0 : 1;

	/* Ensure equal sized t_bit framing for reading / writing bits. */
	udelay(eeprom->timing->t_bit_us.min - eeprom->timing->t_rd_us.min);
}

/*
 * Transmit a logical bit to the AT21CS01 EEPROM device.
 *
 * The mechanism of transmission is for the host to assert the line for either
 * a long or short duration to indicate a logical '0' or '1' respectively to
 * the EEPROM.
 *
 * In the protocol specification these durations are referred to as either
 * t_low0 or t_low1 for logical '0' or '1'.
 */
static void tx_bit(struct at21cs01_eeprom *eeprom, uint8_t bit)
{
	struct gpio_desc *sio = eeprom->sio;
	uint32_t tx_time_us = 0;

	if (bit) {
		/* Transmitting a logical '1'. */
		tx_time_us = eeprom->timing->t_low1_us.min;
	} else {
		/* Transmitting a logical '0'. */
		tx_time_us = eeprom->timing->t_low0_us.min;
	}

	/* Assert the line for requisite duration to indicate bit value. */
	gpiod_set_value(sio, GPIO_ASSERTED);
	udelay(tx_time_us);
	gpiod_set_value(sio, GPIO_RELEASED);

	/* Ensure a fixed framing delay for bit transmission. */
	udelay(eeprom->timing->t_bit_us.min - tx_time_us);
}

/* Receive a byte of data most significant bit first. */
static void rx_byte(struct at21cs01_eeprom *eeprom, uint8_t *byte)
{
	int i;
	uint8_t bit = 0;

	*byte = 0;
	for (i = 7; i >= 0; --i) {
		rx_bit(eeprom, &bit);
		*byte |= (bit << i);
	}
}

/* Transmit a byte of data most significant bit first. */
static void tx_byte(struct at21cs01_eeprom *eeprom, uint8_t byte)
{
	int i;

	for (i = 7; i >= 0; --i) {
		uint8_t bit = (byte >> i) & 1;
		tx_bit(eeprom, bit);
	}
}

/*
 * Enact the reset and discovery sequence to confirm AT21CS01 EEPROM chip is
 * attached to the SI/O gpio. This sequence consists of toggling the gpio in a
 * specific timing as laid out by the AT21CS01 datasheet.
 *
 * See section 4.1.1.1 and 4.1.1.2 of datasheet
 */
static int reset_and_discovery(struct at21cs01_eeprom *eeprom)
{
	int ret = 0;
	unsigned long flags;
	uint8_t val;
	struct gpio_desc *sio = eeprom->sio;

	mutex_lock(&eeprom->lock);
	spin_lock_irqsave(&gpio_bitbang_lock, flags);

	/* Assert the line to toggle reset. */
	gpiod_set_value(sio, GPIO_ASSERTED);
	udelay(T_RESET_US);

	/* Deassert gpio output letting line settle by external pull-up. */
	gpiod_set_value(sio, GPIO_RELEASED);
	/* Wait the device specific rise recovery time. */
	udelay(T_RRT_US);

	/*
	 * Check that the line actually rose. Note that this might not occur if
	 * the requisite pull-up resistor required for communication is not
	 * present.
	 */
	val = gpiod_get_value(sio);
	if (val != GPIO_RELEASED) {
		dev_err(eeprom->dev, "Failure of pull up to raise line level.\n");
		ret = -EIO;
		goto out;
	}

	/* Assert the line again to wake up the device. */
	gpiod_set_value(sio, GPIO_ASSERTED);
	/* Give time for EEPROM to also assert the line. */
	udelay(T_DRR_US);
	gpiod_set_value(sio, GPIO_RELEASED);

	/*
	 * Switch and check if EEPROM is now asserting the line after a delay
	 * of the discovery response time.
	 */
	udelay(T_MSDR_US);
	val = gpiod_get_value(sio);
	if (val != GPIO_ASSERTED) {
		dev_err(eeprom->dev, "Failure of EEPROM to assert line during discovery.\n");
		ret = -ENXIO;
		goto out;
	} else {
		dev_dbg(eeprom->dev, "Successfully responded to discovery.");
	}

out:
	spin_unlock_irqrestore(&gpio_bitbang_lock, flags);
	if (ret == 0) {
		/*
		 * From figure 4-1 in datasheet: finish successful discovery
		 * with delay while eeprom continues to hold line low through
		 * remainder of discovery response acknowledge time.
		 */
		udelay(T_DACK_US - T_MSDR_US);
	}
	mutex_unlock(&eeprom->lock);
	return ret;
}

/*
 * Read the manufacturing identification from the AT21CS01 EEPROM.
 * Communication consists of transmitting the device address byte which
 * includes the opcode / device address and then reading 3 successive bytes
 * from the EEPROM that have a known fixed value.
 *
 * See section 8.5 "Manufacturer ID Read" of datasheet.
 */
static int read_mfg_id(struct at21cs01_eeprom *eeprom)
{
	unsigned long flags;
	int ret = 0;
	uint8_t bit;
	uint8_t byte;
	int i;
	uint8_t device_address = 0;
	uint32_t mfg_id = 0;

	device_address = (AT21CS01_OPCODE_MFG_ID << 4) | (eeprom->client_addr << 1) | 1;
	dev_dbg(eeprom->dev, "Reading mfg id computed dev addr: %x\n", device_address);

	mutex_lock(&eeprom->lock);
	/* Start condition */
	usleep_range(eeprom->timing->t_htss_us.min, eeprom->timing->t_htss_us.max);

	spin_lock_irqsave(&gpio_bitbang_lock, flags);
	/* Send the 8-bits representing command to read manufacturing ID. */
	tx_byte(eeprom, device_address);

	/* Wait for ack from client. */
	rx_bit(eeprom, &bit);
	/* We expect an ack (logical '0') from client. */
	if (bit != 0) {
		dev_err(eeprom->dev, "Did not receive 0 bit from client.\n");
		ret = -EIO;
		goto out;
	}

	/* Begin reading manufacturer identifier (24-bits) most significant byte first. */
	for (i = 2; i >= 0; --i) {
		rx_byte(eeprom, &byte);
		mfg_id |= byte << (i * 8);

		if (i == 0) {
			/* Final received byte is NACKed (logical '1') */
			tx_bit(eeprom, 1);
		} else {
			/* Send host ACK (logical '0') */
			tx_bit(eeprom, 0);
		}
	}

	if (mfg_id != AT21CS01_MFG_ID) {
		/*
		 * We failed to read the expected manufacturing identification
		 * of the device.
		 */
		ret = -ENODEV;
	}

out:
	spin_unlock_irqrestore(&gpio_bitbang_lock, flags);
	dev_dbg(eeprom->dev, "Manufacturer info is: %x\n", mfg_id);
	usleep_range(eeprom->timing->t_htss_us.min, eeprom->timing->t_htss_us.max);
	mutex_unlock(&eeprom->lock);
	return ret;
}

/*
 * Send a speed read/write command to the device, returning ACK (0) or NACK (1).
 *
 * When reading the speed, NACK indicates that the speed mode is not the same
 * value as mode, OR that an error occurred. Two reads are necessary to confirm
 * that the operation was successful.
 *
 * When writing the speed, ACK indicates success, and NACK indicates an error.
 *
 * This function needs to be surrounded by udelay(eeprom->timing->t_htss_us);
 * and a spinlock.
 *
 * This communication is defined in section 6.6.1 "Standard Speed Mode" and
 * 6.6.2 "High Speed Mode" of the datasheet.
 */
static uint8_t send_speed_cmd(
		struct at21cs01_eeprom *eeprom,
		enum data_speed mode,
		enum rw_cmd read)
{
	unsigned long flags;
	uint8_t ack = 0;
	uint8_t device_address = 0;
	uint8_t opcode = (mode == DATA_SPEED_STD)
			? AT21CS01_OPCODE_STD_SPEED : AT21CS01_OPCODE_HIGH_SPEED;

	device_address = (opcode << 4) | (eeprom->client_addr << 1) | (read == SPEED_READ);

	spin_lock_irqsave(&gpio_bitbang_lock, flags);
	tx_byte(eeprom, device_address);
	rx_bit(eeprom, &ack);
	spin_unlock_irqrestore(&gpio_bitbang_lock, flags);

	return ack;
}

/*
 * Read the current device speed configuration, setting mode_out to
 * DATA_SPEED_HIGH or DATA_SPEED_STD.
 *
 * Checking the speed mode returns ACK if it set to the requested speed or NACK
 * otherwise. To catch errors, check both high and standard speed then confirm
 * the results agree.
 *
 * This communication is defined in section 7.6.1 "Standard Speed Mode" of
 * datasheet.
 */
static int get_speed(struct at21cs01_eeprom *eeprom, enum data_speed *mode_out)
{
	int ret = 0;
	uint8_t high_speed_ack, std_speed_ack;

	/* Start condition */
	mutex_lock(&eeprom->lock);
	usleep_range(eeprom->timing->t_htss_us.min, eeprom->timing->t_htss_us.max);

	high_speed_ack = send_speed_cmd(eeprom, DATA_SPEED_HIGH, SPEED_READ);

	/* Restart condition */
	usleep_range(eeprom->timing->t_htss_us.min, eeprom->timing->t_htss_us.max);

	std_speed_ack = send_speed_cmd(eeprom, DATA_SPEED_STD, SPEED_READ);

	usleep_range(eeprom->timing->t_htss_us.min, eeprom->timing->t_htss_us.max);

	if (high_speed_ack == std_speed_ack) {
		dev_err(eeprom->dev,
			"Failure to read speed mode- mode acks the same (high=%d, std=%d)\n",
			 high_speed_ack, std_speed_ack);
		ret = -EIO;
		goto out;
	}
	*mode_out = (high_speed_ack == 0) ? DATA_SPEED_HIGH : DATA_SPEED_STD;

out:
	mutex_unlock(&eeprom->lock);
	return ret;
}

/*
 * Configure device communication to the desired speed mode.
 *
 * This communication is defined in section 7.6.1 "Standard Speed Mode" of
 * datasheet.
 */
static int set_speed(struct at21cs01_eeprom *eeprom, enum data_speed mode)
{
	int ret = 0;
	uint8_t ack;

	/* Start condition */
	mutex_lock(&eeprom->lock);
	usleep_range(eeprom->timing->t_htss_us.min, eeprom->timing->t_htss_us.max);

	ack = send_speed_cmd(eeprom, mode, SPEED_WRITE);
	if (ack == 0)
		eeprom->timing = &data_comm_timings[mode];

	usleep_range(eeprom->timing->t_htss_us.min, eeprom->timing->t_htss_us.max);

	if (ack == 1) {
		dev_err(eeprom->dev, "Failure to set speed mode %s\n",
			(mode == DATA_SPEED_HIGH ? "HIGH" : "STD"));
		ret = -EFAULT;
	}

	mutex_unlock(&eeprom->lock);
	return ret;
}

/*
 * Read and print a new speed mode.
 *
 * This is used when reading the sysfs node.
 */
static ssize_t high_speed_show(
		struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	enum data_speed mode;
	int ret;
	struct at21cs01_eeprom *eeprom = dev_get_drvdata(dev);
	const char *res;

	dev_dbg(eeprom->dev, "Getting EEPROM speed mode\n");

	ret = get_speed(eeprom, &mode);
	if (ret == 0) {
		res = (mode == DATA_SPEED_HIGH ? "on\n" : "off\n");
	} else {
		dev_err(eeprom->dev, "Failed to read speed mode\n");
		res = "error\n";
	}
	return sysfs_emit(buf, res);
}

/*
 * Configure a new speed mode through sysfs.
 *
 * This is used by the sysfs interface.
 */
static ssize_t high_speed_store(
		struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	bool high_speed = 0;
	int ret = 0;
	struct at21cs01_eeprom *eeprom = dev_get_drvdata(dev);
	const char *trim_buf = buf;
	size_t trim_count = count;

	dev_dbg(eeprom->dev, "Changing EEPROM speed mode\n");

	while (trim_count > 0 && isspace(*trim_buf)) {
		trim_buf++;
		trim_count--;
	}
	if (trim_count <= 0) {
		dev_err(eeprom->dev, "Invalid input- all whitespace.\n");
		return -EINVAL;
	}

	/*
	 * trim_buf might not be null-terminated, but kstrobool only checks the
	 * first character or two. We only need to null-terminate it if we print an
	 * error message with the input.
	 */
	ret = kstrtobool(trim_buf, &high_speed);
	if (ret != 0) {
		char *input_buf = kmalloc(count + 1, GFP_KERNEL);
		if (!input_buf) {
			return -ENOMEM;
		}
		memcpy(input_buf, buf, count);
		input_buf[count] = '\0';

		dev_err(
			eeprom->dev,
			"Invalid input. Expected bool (1/y/t/on for high speed), was '%s'\n",
			input_buf);
		kfree(input_buf);
		return -EINVAL;
	}

	ret = set_speed(eeprom, high_speed ? DATA_SPEED_HIGH : DATA_SPEED_STD);
	if (ret != 0) {
		return ret;
	}
	return count;
}

static DEVICE_ATTR_RW(high_speed);

static struct attribute *at21cs01_eeprom_attr_list[] = {
	&dev_attr_high_speed.attr,
	NULL,
};

static struct attribute_group at21cs01_eeprom_attr_grp = {
	.attrs = at21cs01_eeprom_attr_list,
};

static int at21cs01_eeprom_probe(struct platform_device *pdev)
{
	int ret = 0;
	bool std_speed = false;
	struct device *dev = &pdev->dev;
	struct at21cs01_eeprom *eeprom;
	/* Set gpio to open drain and released after init. */
	enum gpiod_flags gflags = GPIOD_OUT_HIGH_OPEN_DRAIN;

	dev_dbg(dev, "Probe start.\n");

	eeprom = devm_kzalloc(&pdev->dev, sizeof(*eeprom), GFP_KERNEL);
	if (!eeprom) {
		dev_err(dev, "Failure to allocate memory for eeprom data\n");
		return -ENOMEM;
	}
	mutex_init(&eeprom->lock);

	platform_set_drvdata(pdev, eeprom);
	dev_set_drvdata(dev, eeprom);
	eeprom->dev = dev;

	eeprom->timing = &data_comm_timings[DATA_SPEED_HIGH];
	dev_dbg(dev, "Data speed timing is set to high\n");

	eeprom->sio = devm_gpiod_get(eeprom->dev, "google,sio", gflags);
	if (IS_ERR(eeprom->sio)) {
		ret = PTR_ERR(eeprom->sio);
		dev_err(eeprom->dev, "Failed to get at21cs01_eeprom SIO pin: %d\n", ret);
		return ret;
	}
	if (!gpiod_is_active_low(eeprom->sio)) {
		dev_err(eeprom->dev, "SI/O GPIO is not configured active low\n");
		return -EINVAL;
	}
	ret = of_property_read_u8(dev->of_node, "google,client_addr", &eeprom->client_addr);
	if (ret != 0) {
		dev_err(eeprom->dev, "Failed to read client address\n");
		eeprom->client_addr = 0;
	}

	dev_dbg(eeprom->dev, "Reset and discover client\n");
	ret = reset_and_discovery(eeprom);
	if (ret != 0) {
		dev_err(eeprom->dev, "Failure to reset and discover: %d\n", ret);
		return ret;
	}

	std_speed = of_property_read_bool(dev->of_node, "google,std-speed");
	if (std_speed) {
		ret = set_speed(eeprom, DATA_SPEED_STD);
		if (ret != 0) {
			dev_err(eeprom->dev, "Failed to set std speed; aborting probe: %d\n", ret);
			return ret;
		}
	}

	ret = read_mfg_id(eeprom);
	if (ret != 0) {
		dev_err(eeprom->dev, "Failure to read manufacturer id: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&eeprom->dev->kobj, &at21cs01_eeprom_attr_grp);
	if (ret != 0) {
		dev_err(dev, "Failed to create sysfs group, errno:%d\n", ret);
	}

	dev_info(eeprom->dev, "Successfully probed.\n");
	return ret;
}

static int at21cs01_eeprom_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct at21cs01_eeprom *eeprom = dev_get_drvdata(dev);

	sysfs_remove_group(&dev->kobj, &at21cs01_eeprom_attr_grp);

	// Reset the device before disconnecting.
	dev_dbg(eeprom->dev, "Reset EEPROM client before removing\n");
	ret = reset_and_discovery(eeprom);
	if (ret != 0) {
		dev_err(eeprom->dev, "Failure to reset and discover: %d\n", ret);
	}

	mutex_destroy(&eeprom->lock);
	dev_info(dev, "Removed device.\n");
	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "at21cs01,eeprom" },
	{ /* sentinel */ }
};

static struct platform_driver at21cs01_eeprom_driver = {
	.driver	= {
		.name = "at21cs01-eeprom",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(match_table),
	},
	.probe	= at21cs01_eeprom_probe,
	.remove	= at21cs01_eeprom_remove,
};

module_platform_driver(at21cs01_eeprom_driver);
MODULE_DESCRIPTION("AT21CS01 EEPROM Driver");
MODULE_AUTHOR("Brandon Edens <brandonedens@google.com>");
MODULE_AUTHOR("Prashant Surana <prashantsurana@google.com>");
MODULE_LICENSE("GPL");
