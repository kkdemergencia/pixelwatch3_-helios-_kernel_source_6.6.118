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

#pragma once

#include <linux/spi/spi.h>

#include "autobahn/transport.h"

/* Create and initialize an abn_Transport object for a full duplex SPI bus.
 * Resources are deallocated automatically when the spi device is unbound.
 *
 * spi_device must point to a valid, initialized spi device.
 *
 * Returns 0 on success, and sets abn_transport to point to the created object.
 */
int abn_transport_fd_spi_create(struct abn_Transport **abn_transport,
				struct spi_device *spi_device);

/* Create and initialize an abn_Transport object for an Ambiq hald-duplex SPI
 * bus. Transfers are half-duplex and conform to specific requirements of Ambiq
 * SPI IOS peripherals.
 * Resources are deallocated automatically when the spi device is unbound.
 *
 * spi_device must point to a valid, initialized spi device.
 *
 * Returns 0 on success, and sets abn_transport to point to the created object.
 */
int abn_transport_amhd_spi_create(struct abn_Transport **abn_transport,
				  struct spi_device *spi_device);

/* Create and initialize an abn_Transport object for an Ambiq full-duplex SPI
 * bus. Transfers are full-duplex and conform to specific requirements of Ambiq
 * SPI IOS peripherals.
 * Resources are deallocated automatically when the spi device is unbound.
 *
 * spi_device must point to a valid, initialized spi device.
 *
 * Returns 0 on success, and sets abn_transport to point to the created object.
 */
int abn_transport_amfd_spi_create(struct abn_Transport **abn_transport,
				  struct spi_device *spi_device);
