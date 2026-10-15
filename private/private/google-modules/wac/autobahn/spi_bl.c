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

#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>

#include "spi.h"
#include "spi_bl.h"

#define CRC_POLY 0x1021

static uint16_t crc_update(uint16_t crc_in, int incr)
{
	uint16_t xor = crc_in >> 15;
	uint16_t out = crc_in << 1;

	if (incr)
		out++;
	if (xor)
		out ^= CRC_POLY;
	return out;
}

// Calculate a packet CRC, ignoring the CRC bytes at offset 4 & 5.
uint16_t bl_crc16(const uint8_t *data, uint16_t size)
{
	uint16_t crc, i, j;

	for (crc = 0, j = 0; j < size; j++, data++) {
		if (j == 4 || j == 5)
			continue; // skip the CRC word
		for (i = 0x80; i; i >>= 1)
			crc = crc_update(crc, *data & i);
	}
	for (i = 0; i < 16; i++)
		crc = crc_update(crc, 0);
	return crc;
}



static int spi_bl_wait(const void *data, enum bl_readiness bl_ready_state)
{
	const struct autobahn_bl *bl = data;
	int retry, delay;

	if (bl_ready_state == BL_READY_NONE)
		return 0;
	for (retry = 0, delay = 0; retry < 20; retry++) {
		if (gpiod_get_raw_value(bl->irq1_gpio) == bl_ready_state)
			return 0;
		delay += 10;
		udelay(delay);
	}
	pr_warn("autobahn: %s timed out waiting for bootloader (%s)\n", __func__,
			bl_ready_state ? "write" : "read");
	return -BL_ERR_TIMEOUT;
}


static int
spi_bl_tx_rx(const struct autobahn_bl *bl, struct spi_transfer *xfer,
	     enum bl_readiness bl_ready_state)
{
	struct spi_message msg;
	int ret;

	spi_message_init_with_transfers(&msg, xfer, 1);
	ret = spi_bl_wait(bl, bl_ready_state);
	if (ret < 0)
		return ret;
	cs_set_value(bl->cs, 0);
	ret = spi_sync_locked(bl->device, &msg);
	cs_set_value(bl->cs, 1);
	return ret;
}


int spi_bl_write_ack(const void *data)
{
	const struct autobahn_bl *bl = data;
	struct spi_transfer xfer = {
		.len = 2,
		.tx_buf = bl->tx_buffer,
		.rx_buf = NULL,
		.cs_change = 1,
	};

	bl->tx_buffer[0] = BL_FRAME_SYNC;
	bl->tx_buffer[1] = BL_FRAME_ACK;

	return spi_bl_tx_rx(bl, &xfer, BL_READY_WRITE);
}

// Read a packet beginning with a sync byte, eg, ACK and Frame packets.
// Works around some bootloader issues that can cause random errors.
static int spi_bl_read_sync_packet(const struct autobahn_bl *bl, int length)
{
	struct spi_transfer xfer = {
		.len = length,
		.tx_buf = NULL,
		.rx_buf = bl->rx_buffer,
		.cs_change = 1,
	};
	int ret;

	ret = spi_bl_tx_rx(bl, &xfer, BL_READY_READ);
	if (ret < 0)
		return ret;

	if (bl->rx_buffer[0] != BL_FRAME_SYNC)
		return -BL_ERR_SYNC;

	if (bl->rx_buffer[1] == BL_FRAME_SYNC) {
		// The NXP bootloader can sometimes return an extra sync byte.
		// Discard the extra sync and refetch the last content byte.
		// This also removes the need to wait 50us b/w sync and ack.
		// See ref manual 18.7.2.6.7
		memmove(bl->rx_buffer, bl->rx_buffer + 1, length - 1);
		xfer.len = 1;
		xfer.rx_buf = bl->rx_buffer + length - 1;
		ret = spi_bl_tx_rx(bl, &xfer, BL_READY_NONE);
	}

	return ret;
}

int spi_bl_read_ack(const void *data)
{
	const struct autobahn_bl *bl = data;

	uint8_t ack;
	int ret = spi_bl_read_sync_packet(bl, 2);

	if (ret < 0)
		return ret;
	ack = bl->rx_buffer[1];
	return ack == BL_FRAME_ACK ? 0 : -ack;
}

// NXP bootloader command and response packet format
// Frame
//    0   sync      5A
//    1   type      A4
//    2   length    2 bytes, length of cmd packet, little endian
//    4   crc       2 bytes, crc of entire packet, not including crc, little endian
// Command or Response
//    6   cmd/resp  1 byte
//    7   flags     1 byte
//    8   reserved  00
//    9   n_args    1 byte
//   10   arg1      4 bytes, little endian
//   14   ...
int
spi_bl_write_cmd(const void *data, uint8_t cmd, uint8_t flags, int n_args, ...)
{
	const struct autobahn_bl *bl = data;
	struct spi_transfer xfer = {
		.len = 10 + 4 * n_args,
		.tx_buf = bl->tx_buffer,
		.rx_buf = NULL,
		.cs_change = 1,
	};
	int i, ret;
	uint16_t crc = 0;
	va_list args;

	bl->tx_buffer[0] = BL_FRAME_SYNC;
	bl->tx_buffer[1] = BL_FRAME_COMMAND;
	put_le16(bl->tx_buffer + 2, 4 + 4 * n_args);
	bl->tx_buffer[6] = cmd;
	bl->tx_buffer[7] = flags;
	bl->tx_buffer[8] = 0;
	bl->tx_buffer[9] = n_args;
	va_start(args, n_args);
	for (i = 0; i < n_args; i++)
		put_le32(bl->tx_buffer + 10 + i * 4, va_arg(args, uint32_t));

	va_end(args);
	crc = bl_crc16(bl->tx_buffer, 10 + 4 * n_args);
	put_le16(bl->tx_buffer + 4, crc);

	ret = spi_bl_tx_rx(bl, &xfer, BL_READY_WRITE);
	if (ret < 0)
		return ret;

	return spi_bl_read_ack(bl);
}


static int spi_bl_read_frame(const struct autobahn_bl *bl)
{
	return spi_bl_read_sync_packet(bl, 6);
}


int spi_bl_write_data(const void *data, const uint8_t *buf, int length)
{
	const struct autobahn_bl *bl = data;
	struct spi_transfer xfer = {
		.len = 6 + length,
		.tx_buf = bl->tx_buffer,
		.rx_buf = NULL,
		.cs_change = 1,
	};
	uint16_t crc = 0;
	int ret;

	bl->tx_buffer[0] = BL_FRAME_SYNC;
	bl->tx_buffer[1] = BL_FRAME_DATA;
	put_le16(bl->tx_buffer + 2, length);
	memcpy(bl->tx_buffer + 6, buf, length);
	crc = bl_crc16(bl->tx_buffer, 6 + length);
	put_le16(bl->tx_buffer + 4, crc);

	ret = spi_bl_tx_rx(bl, &xfer, BL_READY_WRITE);
	if (ret < 0)
		return ret;

	return spi_bl_read_ack(bl);
}


int spi_bl_read_response(const void *data)
{
	const struct autobahn_bl *bl = data;
	struct spi_transfer xfer = {
		.tx_buf = NULL,
		.cs_change = 1,
	};
	int ret;
	uint16_t length, expected_crc, actual_crc;

	// Read the frame
	// This delay allows the MCU to begin a new data ready notification.
	// It could be removed if we changed to an edge-driven interrupt.
	udelay(60);
	ret = spi_bl_read_frame(bl);
	if (ret < 0)
		return ret;

	length = get_le16(bl->rx_buffer + 2);
	expected_crc = get_le16(bl->rx_buffer + 4);

	if (length > 32)
		return -BL_ERR_BAD_RESPONSE;

	// Read the response
	xfer.len = length;
	xfer.rx_buf = bl->rx_buffer + 6;

	ret = spi_bl_tx_rx(bl, &xfer, BL_READY_NONE);
	if (ret < 0)
		return ret;

	actual_crc = bl_crc16(bl->rx_buffer, 6 + length);
	if (actual_crc != expected_crc)
		return -BL_ERR_CRC;

	return spi_bl_write_ack(bl);
}


int spi_bl_open(const void *data)
{
	const struct autobahn_bl *bl = data;

	spi_bus_lock(bl->device->controller);
	return 0;
}

void spi_bl_close(const void *data)
{
	const struct autobahn_bl *bl = data;

	spi_bus_unlock(bl->device->controller);
}


