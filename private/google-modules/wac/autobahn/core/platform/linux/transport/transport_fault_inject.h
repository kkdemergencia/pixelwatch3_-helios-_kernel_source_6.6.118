/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google, Inc.
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

#include <linux/kconfig.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#if IS_ENABLED(CONFIG_AUTOBAHN_FAULT_INJECTION)
void transport_fault_inject_init(const char *name);
void inject_fail_transport_delayed_start(void);
void inject_fail_transport_delayed_confirmation(void);
bool should_fail_transport_spi_error(void);
bool transport_spi_error_should_execute_xfer(void);
void inject_fail_transport_corrupt_tx(uint8_t *tx, size_t size);
void inject_fail_transport_corrupt_rx(uint8_t *rx, size_t size);

static inline int
spi_sync_may_fail(struct spi_device *spi, struct spi_message *message)
{
	if (should_fail_transport_spi_error()) {
		if (transport_spi_error_should_execute_xfer())
			spi_sync(spi, message);

		return -1;
	}

	return spi_sync(spi, message);
}

#else // CONFIG_AUTOBAHN_FAULT_INJECTION

static inline void transport_fault_inject_init(const char *name) {}
static inline void inject_fail_transport_delayed_start(void) {}
static inline void inject_fail_transport_delayed_confirmation(void) {}
static inline void inject_fail_transport_corrupt_tx(uint8_t *tx, size_t size) {}
static inline void inject_fail_transport_corrupt_rx(uint8_t *rx, size_t size) {}

static inline int
spi_sync_may_fail(struct spi_device *spi, struct spi_message *message)
{
	return spi_sync(spi, message);
}

#endif // CONFIG_AUTOBAHN_FAULT_INJECTION
