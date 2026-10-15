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
#ifndef _MCU_DISPLAY_EXPORTS_H_
#define _MCU_DISPLAY_EXPORTS_H_

/**
 * MCU display command types in the command header
 */
enum mcu_display_command_type {
	NANOHUB_DISPLAY_COMMAND_GET_STATE = 6,
	NANOHUB_DISPLAY_COMMAND_SYNC_INFO = 7,
	NANOHUB_DISPLAY_REQUEST_PANEL_INFO = 8,
	NANOHUB_DISPLAY_REQUEST_PANEL_INFO_COMPLETE = 9,
	NANOHUB_DISPLAY_SYNC_PANEL_DCS_COMMAND = 10,
	NANOHUB_DISPLAY_COMMAND_TYPE_MAX,
};

/**
 * MCU display power state returned from mcu_display_query_display_state
 */
enum mcu_display_mode {
	MCU_DISPLAY_NONE = 0,
	MCU_DISPLAY_INIT = 1,
	MCU_DISPLAY_OFF = 2,
	MCU_DISPLAY_IDLE = 3,
	MCU_DISPLAY_ON = 4,
	MCU_DISPLAY_HIGH_BRIGHTNESS = 5,
};

/**
 * Panel cmd type for DCS cmd synchronization
 */
enum mcu_display_panel_command_type {
	MCU_DISPLAY_PANEL_CMD_TYPE_ON = 0,
	MCU_DISPLAY_PANEL_CMD_TYPE_OFF = 1,
	MCU_DISPLAY_PANEL_CMD_TYPE_LP1 = 2,
	MCU_DISPLAY_PANEL_CMD_TYPE_LP2 = 3,
	MCU_DISPLAY_PANEL_CMD_TYPE_NOLP = 4,
	MCU_DISPLAY_PANEL_CMD_TYPE_HBM_ON = 5,
	MCU_DISPLAY_PANEL_CMD_TYPE_HBM_OFF = 6,
	MCU_DISPLAY_PANEL_CMD_TYPE_MAX = 7,
};

/**
 * AP, Kernel and MCU all agree on this
 */
struct __attribute__((packed)) CommandHeader {
	u8 version;
	u8 command;
};

struct display_callback_data {
	const u8 *buf;
	size_t buf_len;
};

struct display_state_data {
	u8 display_mode;
};

struct display_sync_info_data {
	u8 display_mode;
	u8 panel_mode;
	u16 nbm_brightness;
	u16 aod_brightness;
};

/**
 * Callback and Reply func type.
 */
typedef void (*Reply)(u8 cmd_type, void *msg, size_t msg_size);
typedef void (*Callback)(struct display_callback_data *data, void *priv_data, Reply reply);

/**
 * Query the display power state from MCU when display is controlled by MCU.
 * MCU_DISPLAY_NONE, when MCU doesn't control display.
 */
extern int mcu_display_query_display_state(void);

/**
 * Registration function for panel_power_notifier
 **/
extern int mcu_display_register_panel_power_notifier(struct notifier_block *nb);

/**
 * Unregistration function for panel_power_notifier
 **/
extern int mcu_display_unregister_panel_power_notifier(struct notifier_block *nb);

/**
 * Register display callback function to specific command type.
 */
extern int mcu_display_register_listener(enum mcu_display_command_type cmd, Callback cb,
					 void *priv_data);

/**
 * Unregister display callback function.
 */
extern void mcu_display_unregister_listener(enum mcu_display_command_type cmd, Callback cb);

#endif /* _MCU_DISPLAY_EXPORTS_H_ */
