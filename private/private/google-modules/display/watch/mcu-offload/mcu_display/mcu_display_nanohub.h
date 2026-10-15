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
#ifndef _MCU_DISPLAY_NANOHUB_H_
#define _MCU_DISPLAY_NANOHUB_H_

#include "mcu_display_exports.h"

bool mcu_display_register_nanohub(void);

void mcu_display_unregister_nanohub(void);

bool mcu_display_register_listener_internal(u8 cmd_type, Callback cb, void *priv_data);

void mcu_display_unregister_listener_internal(u8 cmd_type, Callback cb);

bool mcu_display_init_internal(void);

void mcu_display_exit_internal(void);

int nanohub_get_display_state(struct display_state_data *data);

int nanohub_sync_display_info(struct display_sync_info_data *data);

#endif /* _MCU_DISPLAY_NANOHUB_H_ */
