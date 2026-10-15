/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCU Transport Interface header
 *
 * Copyright 2026 Google LLC.
 */
#ifndef _MCU_TRANSPORT_OPS_H_
#define _MCU_TRANSPORT_OPS_H_

struct mcu_transport_ops {
	int (*register_channel_listener)(int channel_id,
					 void (*channel_listener)(const char *buffer,
								  size_t length));
	void (*unregister_channel_listener)(int channel_id);
	ssize_t (*send_msg)(int channel_id, const char *buffer, size_t length);
};

int mcu_transport_register_ops(const struct mcu_transport_ops *ops);

void mcu_transport_unregister_ops(void);

#endif // _MCU_TRANSPORT_OPS_H_
