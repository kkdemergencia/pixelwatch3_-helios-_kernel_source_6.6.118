// SPDX-License-Identifier: GPL-2.0
/*
 * MCU Transport Module
 *
 * Copyright 2026 Google LLC.
 */

#define pr_fmt(fmt) "[mcu_transport] " fmt

#include <linux/module.h>
#include <linux/mutex.h>

#include "mcu_transport_log.h"
#include "mcu_transport_interface.h"
#include "mcu_transport_ops.h"

struct mcu_transport_data {
	struct mutex lock;
	const struct mcu_transport_ops *ops;
};

// MCU transport object
static struct mcu_transport_data interface_data = {
	.lock = __MUTEX_INITIALIZER(interface_data.lock),
	.ops = NULL,
};

int mcu_transport_register_ops(const struct mcu_transport_ops *ops)
{
	int ret = 0;

	mutex_lock(&interface_data.lock);
	if (interface_data.ops) {
		LOGE("Comms already registered\n");
		ret = -EBUSY;
		goto out;
	}
	interface_data.ops = ops;
	LOGI("Comms registered\n");
out:
	mutex_unlock(&interface_data.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mcu_transport_register_ops);

void mcu_transport_unregister_ops(void)
{
	mutex_lock(&interface_data.lock);
	interface_data.ops = NULL;
	LOGI("Comms unregistered\n");
	mutex_unlock(&interface_data.lock);
}
EXPORT_SYMBOL_GPL(mcu_transport_unregister_ops);

int mcu_transport_register_channel_listener(int channel_id,
				void (*channel_listener)(const char *buffer,
				size_t length))
{
	int ret = 0;

	mutex_lock(&interface_data.lock);
	if (!interface_data.ops ||
	    !interface_data.ops->register_channel_listener) {
		LOGW("No implementation registered for %s\n", __func__);
		mutex_unlock(&interface_data.lock);
		return -EOPNOTSUPP;
	}
	ret = interface_data.ops->register_channel_listener(channel_id,
							    channel_listener);
	mutex_unlock(&interface_data.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mcu_transport_register_channel_listener);

int mcu_transport_unregister_channel_listener(int channel_id)
{
	int ret = 0;

	mutex_lock(&interface_data.lock);
	if (!interface_data.ops ||
	    !interface_data.ops->unregister_channel_listener) {
		LOGW("No implementation registered for %s\n", __func__);
		mutex_unlock(&interface_data.lock);
		return -EOPNOTSUPP;
	}
	interface_data.ops->unregister_channel_listener(channel_id);
	mutex_unlock(&interface_data.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mcu_transport_unregister_channel_listener);

ssize_t mcu_transport_send_msg(int channel_id,
			       const char *buffer,
			       size_t length)
{
	ssize_t bytes = 0;

	mutex_lock(&interface_data.lock);
	if (!interface_data.ops || !interface_data.ops->send_msg) {
		LOGW("No implementation registered for %s\n", __func__);
		mutex_unlock(&interface_data.lock);
		return -EOPNOTSUPP;
	}
	bytes = interface_data.ops->send_msg(channel_id, buffer, length);
	mutex_unlock(&interface_data.lock);
	return bytes;
}
EXPORT_SYMBOL_GPL(mcu_transport_send_msg);

static int __init mcu_transport_init(void)
{
	LOGI("MCU Transport initialized\n");
	return 0;
}

static void __exit mcu_transport_exit(void)
{
	mcu_transport_unregister_ops();
}

module_init(mcu_transport_init);
module_exit(mcu_transport_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Google Pixel Wear Input Team");
MODULE_DESCRIPTION("MCU Transport Interface");
