// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fault-inject.h>
#include <linux/random.h>
#include <linux/types.h>

#include "transport_fault_inject.h"

#define TRANSPORT_FAULT_ATTR_INITIALIZER {				\
	.interval = 1,							\
	.times = ATOMIC_INIT(-1),					\
	.require_end = ULONG_MAX,					\
	.stacktrace_depth = 32,						\
	.ratelimit_state = RATELIMIT_STATE_INIT_DISABLED,		\
	.verbose = 1,							\
	.dname = NULL,							\
}

struct fault_transport_delay {
	struct fault_attr attr;
	u32 delay_ms;
};

struct fault_transport_spi_error {
	struct fault_attr attr;
	bool execute_xfer;
};

static struct fault_transport_delay fault_transport_delayed_start = {
	.attr = TRANSPORT_FAULT_ATTR_INITIALIZER,
	.delay_ms = 600,
};

static struct fault_transport_delay fault_transport_delayed_confirmation = {
	.attr = TRANSPORT_FAULT_ATTR_INITIALIZER,
	.delay_ms = 600,
};

static struct fault_transport_spi_error fault_transport_spi_error = {
	.attr = TRANSPORT_FAULT_ATTR_INITIALIZER,
	.execute_xfer = false,
};

static struct fault_attr fault_transport_corrupt_tx = TRANSPORT_FAULT_ATTR_INITIALIZER;
static struct fault_attr fault_transport_corrupt_rx = TRANSPORT_FAULT_ATTR_INITIALIZER;

static struct fault_transport_corrupt_tx_rx {
	struct fault_attr attr;
	bool fail_next_rx;
} fault_transport_corrupt_tx_rx = {
	.attr = TRANSPORT_FAULT_ATTR_INITIALIZER,
	.fail_next_rx = false,
};

void transport_fault_inject_init(const char *name)
{
	struct dentry *parent, *dir;

	parent = debugfs_create_dir(name, NULL);
	if (IS_ERR(parent))
		return;

	dir = fault_create_debugfs_attr("transport_delayed_start", parent,
					&fault_transport_delayed_start.attr);
	if (!IS_ERR(dir))
		debugfs_create_u32("delay_ms", 0600, dir,
				   &fault_transport_delayed_start.delay_ms);


	dir = fault_create_debugfs_attr("transport_delayed_confirmation", parent,
					&fault_transport_delayed_confirmation.attr);
	if (!IS_ERR(dir))
		debugfs_create_u32("delay_ms", 0600, dir,
				   &fault_transport_delayed_confirmation.delay_ms);

	dir = fault_create_debugfs_attr("transport_spi_error", parent,
					&fault_transport_spi_error.attr);
	if (!IS_ERR(dir))
		debugfs_create_bool("execute_xfer", 0600, dir,
				   &fault_transport_spi_error.execute_xfer);

	fault_create_debugfs_attr("transport_corrupt_tx", parent,
				  &fault_transport_corrupt_tx);
	fault_create_debugfs_attr("transport_corrupt_rx", parent,
				  &fault_transport_corrupt_rx);
	fault_create_debugfs_attr("transport_corrupt_tx_rx", parent,
				  &fault_transport_corrupt_tx_rx.attr);
}

void inject_fail_transport_delayed_start(void)
{
	if (!should_fail(&fault_transport_delayed_start.attr, 1))
		return;

	msleep(fault_transport_delayed_start.delay_ms);
}

void inject_fail_transport_delayed_confirmation(void)
{
	if (!should_fail(&fault_transport_delayed_confirmation.attr, 1))
		return;

	msleep(fault_transport_delayed_confirmation.delay_ms);
}

bool should_fail_transport_spi_error(void)
{
	return should_fail(&fault_transport_spi_error.attr, 1);
}

bool transport_spi_error_should_execute_xfer(void)
{
	return fault_transport_spi_error.execute_xfer;
}

static void flip_random_byte(uint8_t *data, size_t size)
{
	size_t pos = get_random_u32_below(size);

	data[pos] = ~data[pos];
}

void inject_fail_transport_corrupt_tx(uint8_t *tx, size_t size)
{
	if (should_fail(&fault_transport_corrupt_tx_rx.attr, 1))
		fault_transport_corrupt_tx_rx.fail_next_rx = true;
	else if (!should_fail(&fault_transport_corrupt_tx, 1))
		return;

	flip_random_byte(tx, size);
}

void inject_fail_transport_corrupt_rx(uint8_t *rx, size_t size)
{
	if (fault_transport_corrupt_tx_rx.fail_next_rx)
		fault_transport_corrupt_tx_rx.fail_next_rx = false;
	else if (!should_fail(&fault_transport_corrupt_rx, 1))
		return;

	flip_random_byte(rx, size);
}
