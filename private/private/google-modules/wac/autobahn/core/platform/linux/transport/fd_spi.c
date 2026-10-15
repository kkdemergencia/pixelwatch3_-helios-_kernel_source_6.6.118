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

/* Full duplex SPI Autobahn transport implementation. If the peripheral
 * supports it, this is the preferred transport implementation for a SPI bus.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "autobahn/log/log.h"
#include "autobahn/platform/linux/kunit.h"
#include "autobahn/transport.h"

enum fd_spi_transport_state {
	FD_SPI_TRANSPORT_STATE_IDLE,     /* Ready to prepare a new transfer. */
	FD_SPI_TRANSPORT_STATE_PREPARED, /* Ready to initiate a transfer. */
};

/* Private abn_Transport data, stored in transport_handle. */
struct abn_Transport_Priv {
	enum fd_spi_transport_state state;
	abn_Transport_ResultHandler result_handler;
	void *handler_context;
	struct spi_device *spi_device;
	struct spi_message spi_message;
	struct spi_transfer spi_transfer;
};

static void
fd_spi_set_result_handler(void *transport_handle,
			  abn_Transport_ResultHandler result_handler,
			  void *handler_context);

static void fd_spi_prepare_transfer(void *transport_handle,
				    uint8_t *tx_data,
				    uint8_t *rx_data,
				    size_t size_bytes);

static void fd_spi_initiate_transfer(void *transport_handle);

static void fd_spi_abort_transfer(void *transport_handle);

static const struct abn_Transport_Functions abn_transport_functions = {
	fd_spi_set_result_handler,
	fd_spi_prepare_transfer,
	fd_spi_initiate_transfer,
	fd_spi_abort_transfer
};

int __mockable fd_spi_spi_sync_wrapper(struct spi_device *spi,
		struct spi_message *message)
{
	return spi_sync(spi, message);
}

void __mockable
fd_spi_spi_message_init_with_transfers_wrapper(struct spi_message *m,
					       struct spi_transfer *xfers,
					       unsigned int num_xfers)
{
	return spi_message_init_with_transfers(m, xfers, num_xfers);
}

static void
fd_spi_set_result_handler(void *transport_handle,
			  abn_Transport_ResultHandler result_handler,
			  void *handler_context)
{
	struct abn_Transport_Priv *priv =
		(struct abn_Transport_Priv *)transport_handle;

	ABN_ASSERT(priv != NULL);
	ABN_ASSERT(result_handler != NULL);

	priv->result_handler = result_handler;
	priv->handler_context = handler_context;
}

static void fd_spi_prepare_transfer(void *transport_handle,
				    uint8_t *tx_data,
				    uint8_t *rx_data,
				    size_t size_bytes)
{
	struct abn_Transport_Priv *priv =
		(struct abn_Transport_Priv *)transport_handle;

	ABN_ASSERT(priv != NULL);
	ABN_ASSERT(priv->result_handler != NULL);
	ABN_ASSERT(tx_data != NULL);
	ABN_ASSERT(rx_data != NULL);

	/* Verify that we are in idle state. No way report error, so we assert
	 * since it's a bug in the caller's code.
	 *
	 * The spi complete callback may still be running at this point, since the
	 * current thread may have been unblocked by the result_handler. Acquiring
	 * the spinlock ensures that the interrupt handler is out of the critical
	 * section.
	 */
	ABN_ASSERT(priv->state == FD_SPI_TRANSPORT_STATE_IDLE);

	priv->spi_transfer.tx_buf = tx_data;
	priv->spi_transfer.rx_buf = rx_data;
	priv->spi_transfer.len = size_bytes;

	fd_spi_spi_message_init_with_transfers_wrapper(
			&priv->spi_message,
			&priv->spi_transfer,
			1);

	priv->state = FD_SPI_TRANSPORT_STATE_PREPARED;
}

static void fd_spi_initiate_transfer(void *transport_handle)
{
	int ret;
	struct abn_Transport_Priv *priv =
		(struct abn_Transport_Priv *)transport_handle;

	ABN_ASSERT(priv != NULL);
	ABN_ASSERT(priv->state == FD_SPI_TRANSPORT_STATE_PREPARED);

	ret = fd_spi_spi_sync_wrapper(priv->spi_device, &priv->spi_message);
	if (ret < 0)
		ABN_LOG_ERROR("Failed to execute SPI transfer: %d", ret);

	priv->result_handler(priv->handler_context, (ret == 0));

	priv->state = FD_SPI_TRANSPORT_STATE_IDLE;
}

static void fd_spi_abort_transfer(void *transport_handle)
{
	struct abn_Transport_Priv *priv =
		(struct abn_Transport_Priv *)transport_handle;

	ABN_ASSERT(priv != NULL);

	switch (priv->state) {
	case FD_SPI_TRANSPORT_STATE_IDLE:
		break;
	case FD_SPI_TRANSPORT_STATE_PREPARED:
		priv->state = FD_SPI_TRANSPORT_STATE_IDLE;
		break;
	default:
		ABN_LOG_FATAL("Unexpected fd_spi_transport_state: %d", priv->state);
	}
}

int abn_transport_fd_spi_create(struct abn_Transport **abn_transport,
				struct spi_device *spi_device)
{
	struct abn_Transport *mabn_transport;
	struct abn_Transport_Priv *priv;

	ABN_ASSERT(abn_transport != NULL);
	ABN_ASSERT(spi_device != NULL);

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
