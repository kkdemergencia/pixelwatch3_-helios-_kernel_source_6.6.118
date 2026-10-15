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

#ifndef _NANOHUB_EXPORTS_H_
#define _NANOHUB_EXPORTS_H_

#include <linux/types.h>

/* Symbols exported to other kernel modules. */

#define NANOHUB_AUDIO_CHANNEL_ID           16
#define NANOHUB_DISPLAY_CHANNEL_ID         17
#define NANOHUB_TOUCH_CHANNEL_ID           25
#define NANOHUB_DISPLAY_KERNEL_CHANNEL_ID  26
#define NANOHUB_TOUCH_KERNEL_CHANNEL_ID    29
#define NANOHUB_TIME_SYNC_CHANNEL_ID       31
#define NANOHUB_RSB_CHANNEL_ID             34
#define NANOHUB_I2C_ARBITRATION_CHANNEL_ID 35

/**
 * Sends a message over a nanohub channel.
 *
 * Remote delivery and dispatch of the entire buffer is guaranteed on
 * successful return.
 *
 * Returns the size of the buffer transferred, or an error if < 0.
 */
extern ssize_t nanohub_send_message(int channel_id, const char *buffer,
				    size_t length);

/**
 * Register a message listener of a nanohub channel.
 *
 * on_message_received callback will be invoked when nanohub receives
 * a new message from MCU on that channel.
 *
 * Returns false if nanohub is not ready for access.
 */
extern bool nanohub_register_listener(
	int channel_id,
	void (*on_message_received)(const char *buffer, size_t length));

/**
 * Unregister the message listener of a nanohub channel.
 */
extern void nanohub_unregister_listener(int channel_id);

#endif /* _NANOHUB_EXPORTS_H_ */
