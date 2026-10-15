/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCU Transport Interface header
 *
 * Copyright 2026 Google LLC.
 */
#ifndef _MCU_TRANSPORT_H_
#define _MCU_TRANSPORT_H_

#define MCU_TRANSPORT_DISPLAY_CHANNEL_ID	 17
#define MCU_TRANSPORT_TOUCH_CHANNEL_ID		 25
#define MCU_TRANSPORT_TOUCH_KERNEL_CHANNEL_ID	 29
#define MCU_TRANSPORT_RSB_CHANNEL_ID		 34
#define MCU_TRANSPORT_I2C_ARBITRATION_CHANNEL_ID 35

typedef void (*channel_handler)(const char *in_buffer, size_t len);

extern int mcu_transport_register_channel_listener(int channel_id,
						   channel_handler handler);

extern int mcu_transport_unregister_channel_listener(int channel_id);

extern ssize_t mcu_transport_send_msg(int channel_id,
				      const char *buffer,
				      size_t length);

#endif // _MCU_TRANSPORT_H_
