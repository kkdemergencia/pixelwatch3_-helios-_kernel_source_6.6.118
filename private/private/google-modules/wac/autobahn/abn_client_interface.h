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

#ifndef _AUTOBAHN_CLIENT_INTERFACE_H
#define _AUTOBAHN_CLIENT_INTERFACE_H

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/device.h>

#include "autobahn/llc/llc_interface.h"
#include "channel_specific_cfg.h"

/* This file describes an Autobahn client interface. The Autobahn kernel
 * driver may expose multiple interfaces for an Autobahn link.
 *
 * Each Autobahn client interface implements a character device interface
 * and/or a kernel interface that the module exposes with EXPORT_SYMBOL().
 *
 * Each client interface is bound to a distinct channel range in the total
 * usable Autobahn channel space. A single instance of each client interface
 * type may be initialized at one time.
 *
 * The driver is expected to initialize the Autobahn core library and manage
 * all its kernel resources. The driver is then expected to register a
 * character device, initialize the Autobahn client interfaces and to route
 * calls on the character device to the appropriate client interface, based
 * on the requested channel.
 */

struct abn_client_interface;
struct abn_client_interface_config;

/* Initialize the Autobahn client interface. Each interface must provide
 * its implementation for this method.
 *
 * config    - Interface configuration passed by the caller.
 * interface - Pointer to the fully initialized interface returned to the
 *             caller.
 *
 * Returns 0 on success or negated errno on failure.
 */
typedef int (*abn_client_interface_init)(
	struct abn_client_interface_config *config,
	struct abn_client_interface **interface);

struct abn_client_interface_config {
	/* Range of Autobahn channels that this interface is bound to. */
	u8 channel_range[2];

	/* Pointer to a fully initialized LLC interface instance. This
	 * interface is shared between all client interfaces.
	 */
	struct abn_llc_Llc_Interface *llc_interface;

	/* Pointer to the first element of an array with specific/non default
	 * channels configuration. It is set to NULL if no specific
	 * configuration was defined.
	 */
	const struct channel_specific_cfg *channels_specific_cfg;
	size_t channels_specific_cfg_size;
};

struct abn_client_interface_ops {
	/* Deinitialize the Autobahn client interface. After this function
	 * returns, it is safe to destroy the abn_StreamingChannel instance.
	 * This handler should close all channels that it opened and release
	 * any allocated resources.
	 */
	void (*deinit)(struct abn_client_interface *interface);

	/* Create the device nodes associated with the client interface. */
	int (*create_sysfs_nodes)(void *data, int major);

	/* Destroy the device nodes associated with the client interface. */
	void (*destroy_sysfs_nodes)(void);

	/* Character device file operations implemented by the client
	 * interface.
	 */
	const struct file_operations *fops;
};

struct abn_client_interface {
	struct abn_client_interface_config *config;
	const struct abn_client_interface_ops *ops;
	void *priv;
};

#endif /* _AUTOBAHN_CLIENT_INTERFACE_H */
