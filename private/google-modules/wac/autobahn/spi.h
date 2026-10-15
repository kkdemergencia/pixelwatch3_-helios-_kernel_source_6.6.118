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

#ifndef _AUTOBAHN_SPI_H
#define _AUTOBAHN_SPI_H

#include "main.h"

struct autobahn_spi_data {
	struct autobahn_data data;
	struct spi_device *spi_dev;
	int cs;
};

int __init autobahn_spi_init(void);

void autobahn_spi_cleanup(void);

void cs_set_value(int cs, int value);

void autobahn_spi_bl_init(struct autobahn_spi_data *spi_data);

#endif
