// SPDX-License-Identifier: GPL-2.0-only
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

/* Ambiq half-duplex SPI Autobahn transport implementation.
 *
 * Tx/Rx is split in separate transactions. Tx is further split to
 * accommodate the rx size limit of the Ambiq peripheral.
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/align.h>
#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/minmax.h>
#include <linux/panic.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>

#include "autobahn/autobahn.h"
#include "autobahn/transport.h"

/* Ambiq half-duplex SPI peripheral expects transfers done in the format
 * 1 byte command write + n byte data read/write.
 */
#define AMHD_SPIHEADER_SIZE	1
#define AMHD_WRITE_FLAG	0x80
#define AMHD_READ_FLAG	0x00
#define AMHD_FIFO_ADDR	0x7F
#define AMHD_PADDING_VALUE	0xFF
#define AMHD_REGSTART_ADDR	0x0
#define AMHD_PAYLOAD_ALIGNMENT	4

#define AMHD_READ_CMD	((AMHD_READ_FLAG)|(AMHD_FIFO_ADDR))
#define AMHD_WRITE_CMD	((AMHD_WRITE_FLAG)|(AMHD_REGSTART_ADDR))

/* There is no flow control between the split transfers. Insert a delay
 * between CS deassert and assert to give time for the responder to process
 * and prepare for the next transfer.
 */
#define AMHD_CS_CHANGE_DELAY_US 100

#define AMHD_WRITE_MAX_BYTES	80
__static_assert(AMHD_WRITE_MAX_BYTES % 4 == 0,
		"Payload size must be multiple of 4");

#define AMHD_WRITE_XFER_SIZE	((AMHD_SPIHEADER_SIZE) + (AMHD_WRITE_MAX_BYTES))
#define AMHD_READ_XFER_SIZE						\
	((AMHD_SPIHEADER_SIZE) + ALIGN(ABN_FRAME_SIZE, AMHD_PAYLOAD_ALIGNMENT))

#define AMHD_NUM_WRITE_TRANSFERS					\
	(((ABN_FRAME_SIZE) + (AMHD_WRITE_MAX_BYTES) - 1) / (AMHD_WRITE_MAX_BYTES))
#define AMHD_NUM_READ_TRANSFERS	1
#define AMHD_NUM_TRANSFERS ((AMHD_NUM_WRITE_TRANSFERS) + (AMHD_NUM_READ_TRANSFERS))

enum amhd_transport_state {
	AMHD_TRANSPORT_STATE_IDLE,     /* Ready to prepare a new transfer. */
	AMHD_TRANSPORT_STATE_PREPARED, /* Ready to initiate a transfer. */
};

struct amhd_spi_priv {
	struct spi_device *spi_device;
	enum amhd_transport_state state;

	abn_Transport_ResultHandler result_handler;
	void *handler_context;
	uint8_t *saved_rx_data;
	size_t saved_size_bytes;

	struct spi_message message;
	struct spi_transfer xfer[AMHD_NUM_TRANSFERS];

	uint8_t write_tx_buf[AMHD_NUM_WRITE_TRANSFERS][AMHD_WRITE_XFER_SIZE];
	uint8_t read_tx_buf[AMHD_READ_XFER_SIZE];
	uint8_t read_rx_buf[AMHD_READ_XFER_SIZE];
};

static void amhd_set_result_handler(void *transport_handle,
				    abn_Transport_ResultHandler result_handler,
				    void *handler_context);
static void amhd_prepare_transfer(void *transport_handle,
				  uint8_t *tx_data,
				  uint8_t *rx_data,
				  size_t size_bytes);
static void amhd_initiate_transfer(void *transport_handle);
static void amhd_abort_transfer(void *transport_handle);

static const struct abn_Transport_Functions abn_transport_functions = {
	amhd_set_result_handler,
	amhd_prepare_transfer,
	amhd_initiate_transfer,
	amhd_abort_transfer,
};

static void amhd_set_result_handler(void *transport_handle,
				    abn_Transport_ResultHandler result_handler,
				    void *handler_context)
{
	struct amhd_spi_priv *priv = (struct amhd_spi_priv *)transport_handle;

	BUG_ON(priv == NULL);
	BUG_ON(result_handler == NULL);

	priv->result_handler = result_handler;
	priv->handler_context = handler_context;
}

static void amhd_prepare_transfer(void *transport_handle,
				  uint8_t *tx_data,
				  uint8_t *rx_data,
				  size_t size_bytes)
{
	struct amhd_spi_priv *priv = (struct amhd_spi_priv *)transport_handle;
	size_t current_offset = 0;
	size_t current_xfer = 0;

	BUG_ON(priv == NULL);
	BUG_ON(priv->result_handler == NULL);
	BUG_ON(tx_data == NULL);
	BUG_ON(rx_data == NULL);
	BUG_ON(size_bytes > ABN_FRAME_SIZE);

	/* Verify that we are in idle state. No way report error, so we assert
	 * since it's a bug in the caller's code.
	 */
	BUG_ON(priv->state != AMHD_TRANSPORT_STATE_IDLE);

	memset(&priv->xfer, 0, sizeof(priv->xfer));

	/* Prepare the write transfers. */
	while (current_offset < size_bytes) {
		/* Fragment the payload in AMHD_WRITE_MAX_BYTES transfers. */
		size_t current_len = min(size_bytes - current_offset,
					 (size_t)AMHD_WRITE_MAX_BYTES);

		/* Transfer size is 1 byte command + payload + optional
		 * alignment padding.
		 */
		size_t xfer_len =
			ALIGN(current_len, AMHD_PAYLOAD_ALIGNMENT) + 1;

		priv->write_tx_buf[current_xfer][0] = AMHD_WRITE_CMD;
		memcpy(&priv->write_tx_buf[current_xfer][1],
		       tx_data + current_offset, current_len);

		/* If the transfer is larger than the payload for alignment
		 * reasons, set the padding value.
		 */
		memset(&priv->write_tx_buf[current_xfer][1 + current_len],
		       AMHD_PADDING_VALUE, xfer_len - current_len - 1);

		priv->xfer[current_xfer].tx_buf =
			priv->write_tx_buf[current_xfer];
		priv->xfer[current_xfer].len = xfer_len;
		priv->xfer[current_xfer].cs_change = 1;
		priv->xfer[current_xfer].cs_change_delay.value =
			AMHD_CS_CHANGE_DELAY_US;
		priv->xfer[current_xfer].cs_change_delay.unit =
			SPI_DELAY_UNIT_USECS;

		current_xfer += 1;
		current_offset += current_len;
	}

	/* Prepare the read transfer. Initiator transmits only the 1 byte read
	 * command, so we set the padding value for the rest of the buffer.
	 */
	priv->read_tx_buf[0] = AMHD_READ_CMD;
	memset(&priv->read_tx_buf[1], AMHD_PADDING_VALUE,
	       sizeof(priv->read_tx_buf) - 1);

	priv->xfer[current_xfer].tx_buf = priv->read_tx_buf;
	priv->xfer[current_xfer].rx_buf = priv->read_rx_buf;

	/* Transfer size is aligned to AMHD_PAYLOAD_ALIGNMENT. If the responder
	 * tries to send more data than the requested size and some bytes fall
	 * under the alignment padding, they will be ignored.
	 */
	priv->xfer[current_xfer].len =
		ALIGN(size_bytes, AMHD_PAYLOAD_ALIGNMENT) + 1;
	priv->xfer[current_xfer].cs_change = 1;

	current_xfer += 1;

	spi_message_init_with_transfers(&priv->message, priv->xfer,
					current_xfer);

	/* Store the requested rx buffer. Will be used in initiate transfer to
	 * copy data after the transfer completes.
	 */
	priv->saved_rx_data = rx_data;
	priv->saved_size_bytes = size_bytes;

	priv->state = AMHD_TRANSPORT_STATE_PREPARED;
}

static void amhd_initiate_transfer(void *transport_handle)
{
	int ret;
	struct amhd_spi_priv *priv = (struct amhd_spi_priv *)transport_handle;

	BUG_ON(priv == NULL);
	BUG_ON(priv->state != AMHD_TRANSPORT_STATE_PREPARED);

	ret = spi_sync(priv->spi_device, &priv->message);
	if (ret < 0)
		pr_err("Failed to execute SPI transfer: %d", ret);
	else
		/* Received data is offset by 1 byte because the first byte
		 * corresponds to the read command sent by the initiator.
		 */
		memcpy(priv->saved_rx_data, &priv->read_rx_buf[1],
		       priv->saved_size_bytes);

	priv->result_handler(priv->handler_context, (ret == 0));
	priv->state = AMHD_TRANSPORT_STATE_IDLE;
}

static void amhd_abort_transfer(void *transport_handle)
{
	struct amhd_spi_priv *priv = (struct amhd_spi_priv *)transport_handle;

	BUG_ON(priv == NULL);

	switch (priv->state) {
	case AMHD_TRANSPORT_STATE_IDLE:
		break;
	case AMHD_TRANSPORT_STATE_PREPARED:
		priv->state = AMHD_TRANSPORT_STATE_IDLE;
		break;
	default:
		panic("Unexpected transport state: %d", priv->state);
	}
}

int abn_transport_amhd_spi_create(struct abn_Transport **abn_transport,
				  struct spi_device *spi_device)
{
	struct abn_Transport *mabn_transport;
	struct amhd_spi_priv *priv;

	BUG_ON(abn_transport == NULL);
	BUG_ON(spi_device == NULL);

	priv = devm_kzalloc(&spi_device->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	mabn_transport = devm_kzalloc(&spi_device->dev, sizeof(*mabn_transport),
				      GFP_KERNEL);
	if (mabn_transport == NULL)
		return -ENOMEM;

	priv->spi_device = spi_device;
	mabn_transport->transport_handle = priv;
	mabn_transport->functions = &abn_transport_functions;

	*abn_transport = mabn_transport;

	return 0;
}
