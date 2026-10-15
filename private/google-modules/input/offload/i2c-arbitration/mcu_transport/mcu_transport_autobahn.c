// SPDX-License-Identifier: GPL-2.0
/*
 * MCU transport autobahn interface
 *
 * Copyright 2026 Google LLC.
 */

#define pr_fmt(fmt) "[mcu_transport_autobahn] " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "mcu_transport_log.h"
#include "mcu_transport_interface.h"
#include "mcu_transport_ops.h"
#include "nanohub_exports.h"

static int autobahn_register_channel_listener(int channel_id,
					      void (*channel_listener)(const char *buffer,
								       size_t length));
static void autobahn_unregister_channel_listener(int channel_id);
static ssize_t autobahn_send_msg(int channel_id, const char *buffer, size_t length);

static const struct mcu_transport_ops autobahn_ops = {
	.register_channel_listener = autobahn_register_channel_listener,
	.unregister_channel_listener = autobahn_unregister_channel_listener,
	.send_msg = autobahn_send_msg,
};

int autobahn_register_channel_listener(int channel_id,
				       void (*channel_listener)(const char *buffer, size_t length))
{
	if (!channel_listener)
		return -EINVAL;
	return (true == nanohub_register_listener(channel_id, channel_listener))
		? 0
		: -EINVAL;
}

void autobahn_unregister_channel_listener(int channel_id)
{
	nanohub_unregister_listener(channel_id);
}

ssize_t autobahn_send_msg(int channel_id, const char *buffer, size_t length)
{
	if (!buffer || !length)
		return -EINVAL;

	return nanohub_send_message(channel_id, buffer, length);
}

static int __init mcu_transport_autobahn_init(void)
{
	LOGI("MCU Transport Autobahn initialized\n");
	mcu_transport_register_ops(&autobahn_ops);
	return 0;
}

static void __exit mcu_transport_autobahn_exit(void)
{
	mcu_transport_unregister_ops();
}

module_init(mcu_transport_autobahn_init);
module_exit(mcu_transport_autobahn_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Google Pixel Wear Input Team");
MODULE_DESCRIPTION("MCU Transport Autobahn interface");
