/* SPDX-License-Identifier: GPL-2.0-only */
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

#ifndef _AUTOBAHN_SPI_BL_H
#define _AUTOBAHN_SPI_BL_H

enum bl_readiness {
	BL_READY_READ = 0,
	BL_READY_WRITE = 1,
	BL_READY_NONE = 2,
};

int spi_bl_write_ack(const void *data);
int spi_bl_read_ack(const void *data);
int
spi_bl_write_cmd(const void *data, uint8_t cmd, uint8_t flags, int n_args, ...);
int spi_bl_write_data(const void *data, const uint8_t *buf, int length);
int spi_bl_read_response(const void *data);
int spi_bl_open(const void *data);
void spi_bl_close(const void *data);


#endif
