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

/* Ambiq full-duplex SPI Autobahn transport implementation. */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>

#include "autobahn/autobahn.h"
#include "autobahn/transport.h"
#include "transport_fault_inject.h"

/* Ambiq full-duplex SPI peripheral expects transfers done in the format
 * 1 byte FIFO address + n byte data read/write.
 */
#define AMFD_SPIHEADER_SIZE	1
#define AMFD_FIFO_ADDR	0x7F

#define AMFD_TRANSFER_SIZE	((AMFD_SPIHEADER_SIZE) + (ABN_FRAME_SIZE))

enum amfd_transport_state {
	AMFD_TRANSPORT_STATE_IDLE,     /* Ready to prepare a new transfer. */
	AMFD_TRANSPORT_STATE_PREPARED, /* Ready to initiate a transfer. */
};

struct amfd_spi_priv {
	struct spi_device *spi_device;
	enum amfd_transport_state state;

	abn_Transport_ResultHandler result_handler;
	void *handler_context;
	uint8_t *saved_rx_data;
	size_t saved_size_bytes;

	struct spi_message message;
	struct spi_transfer spi_transfer;

	uint8_t tx_buf[AMFD_TRANSFER_SIZE];
	uint8_t rx_buf[AMFD_TRANSFER_SIZE];
};

static void amfd_set_result_handler(void *transport_handle,
				    abn_Transport_ResultHandler result_handler,
				    void *handler_context);
static void amfd_prepare_transfer(void *transport_handle,
				  uint8_t *tx_data,
				  uint8_t *rx_data,
				  size_t size_bytes);
static void amfd_initiate_transfer(void *transport_handle);
static void amfd_abort_transfer(void *transport_handle);

static const struct abn_Transport_Functions abn_transport_functions = {
	amfd_set_result_handler,
	amfd_prepare_transfer,
	amfd_initiate_transfer,
	amfd_abort_transfer,
};

static void amfd_set_result_handler(void *transport_handle,
				    abn_Transport_ResultHandler result_handler,
				    void *handler_context)
{
	struct amfd_spi_priv *priv = (struct amfd_spi_priv *)transport_handle;

	priv->result_handler = result_handler;
	priv->handler_context = handler_context;
}

static void amfd_prepare_transfer(void *transport_handle,
				  uint8_t *tx_data,
				  uint8_t *rx_data,
				  size_t size_bytes)
{
	struct amfd_spi_priv *priv = (struct amfd_spi_priv *)transport_handle;

	if (tx_data == NULL || rx_data == NULL || size_bytes == 0 ||
	    size_bytes > ABN_FRAME_SIZE) {
		WARN(1, "Invalid argument! tx_data: %p rx_data: %p size_bytes: %zu",
		     tx_data, rx_data, size_bytes);
		return;
	}

	if (priv->result_handler == NULL) {
		WARN(1, "Result handler not registered!");
		return;
	}

	WARN_ON(priv->state != AMFD_TRANSPORT_STATE_IDLE);

	memset(&priv->spi_transfer, 0, sizeof(priv->spi_transfer));

	priv->tx_buf[0] = AMFD_FIFO_ADDR;
	memcpy(priv->tx_buf + AMFD_SPIHEADER_SIZE, tx_data, size_bytes);

	priv->spi_transfer.tx_buf = priv->tx_buf;
	priv->spi_transfer.rx_buf = priv->rx_buf;
	priv->spi_transfer.len = size_bytes + AMFD_SPIHEADER_SIZE;

	/* Store the requested rx buffer. Will be used in amfd_initiate_transfer()
	 * to copy data after the transfer completes.
	 */
	priv->saved_rx_data = rx_data;
	priv->saved_size_bytes = size_bytes;

	spi_message_init_with_transfers(&priv->message, &priv->spi_transfer, 1);

	priv->state = AMFD_TRANSPORT_STATE_PREPARED;
}

static void amfd_initiate_transfer(void *transport_handle)
{
	int ret;
	struct amfd_spi_priv *priv = (struct amfd_spi_priv *)transport_handle;

	if (priv->state != AMFD_TRANSPORT_STATE_PREPARED) {
		WARN(1, "Invalid transport state: %d", priv->state);

		if (priv->result_handler)
			priv->result_handler(priv->handler_context, false);
		return;
	}

	inject_fail_transport_delayed_start();
	inject_fail_transport_corrupt_tx(&priv->tx_buf[AMFD_SPIHEADER_SIZE],
					 priv->saved_size_bytes);

	ret = spi_sync_may_fail(priv->spi_device, &priv->message);
	if (ret < 0)
		pr_err("Failed to execute SPI transfer: %d", ret);
	else
		/* Received data is offset by 1 byte because the first byte
		 * corresponds to the FIFO address sent by the initiator.
		 */
		memcpy(priv->saved_rx_data, &priv->rx_buf[AMFD_SPIHEADER_SIZE],
		       priv->saved_size_bytes);

	inject_fail_transport_corrupt_rx(priv->saved_rx_data,
					 priv->saved_size_bytes);
	inject_fail_transport_delayed_confirmation();

	if (priv->result_handler)
		priv->result_handler(priv->handler_context, (ret == 0));

	priv->state = AMFD_TRANSPORT_STATE_IDLE;
}

static void amfd_abort_transfer(void *transport_handle)
{
	struct amfd_spi_priv *priv = (struct amfd_spi_priv *)transport_handle;

	switch (priv->state) {
	case AMFD_TRANSPORT_STATE_IDLE:
		break;
	case AMFD_TRANSPORT_STATE_PREPARED:
		priv->state = AMFD_TRANSPORT_STATE_IDLE;
		break;
	default:
		panic("Unexpected transport state: %d", priv->state);
	}
}

int abn_transport_amfd_spi_create(struct abn_Transport **abn_transport,
				  struct spi_device *spi_device)
{
	struct abn_Transport *mabn_transport;
	struct amfd_spi_priv *priv;

	if (abn_transport == NULL || spi_device == NULL)
		return -EINVAL;

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

	transport_fault_inject_init("fault_amfd_spi");

	return 0;
}
