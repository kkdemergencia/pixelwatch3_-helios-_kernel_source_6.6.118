// SPDX-License-Identifier: GPL-2.0
/*
 * I2C Arbitration Interface Module
 *
 * Copyright 2026 Google LLC.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>

#include "i2c_arbitration_module.h"
#include "i2c_arbitration_ops.h"
#include "i2c_arbitration_log.h"

struct i2c_arbitration_event_notifier {
	int (*callback)(void *client_data, enum i2c_arbitration_event_type);
	void *client_data;
	struct list_head list;
};

struct i2c_arbitration_interface_data {
	struct mutex lock; /* Protects ops and notifier list */
	const struct i2c_arbitration_ops *ops;
};

static struct i2c_arbitration_interface_data interface_data;

void offloading_request_take_control_back_ap(void)
{
	mutex_lock(&interface_data.lock);
	if (!interface_data.ops || !interface_data.ops->take_control_back_ap) {
		LOGW("No implementation registered for %s\n", __func__);
		mutex_unlock(&interface_data.lock);
		return;
	}
	interface_data.ops->take_control_back_ap();
	mutex_unlock(&interface_data.lock);
}
EXPORT_SYMBOL_GPL(offloading_request_take_control_back_ap);

void offloading_request_yield_control_to_mcu(void)
{
	mutex_lock(&interface_data.lock);
	if (!interface_data.ops || !interface_data.ops->yield_control_to_mcu) {
		LOGW("No implementation registered for %s\n", __func__);
		mutex_unlock(&interface_data.lock);
		return;
	}
	interface_data.ops->yield_control_to_mcu();
	mutex_unlock(&interface_data.lock);
}
EXPORT_SYMBOL_GPL(offloading_request_yield_control_to_mcu);

int register_i2c_arbitration_event_notifier(void *client_data,
					    int (*callback)(void *client_data,
							    enum i2c_arbitration_event_type))
{
	int ret;

	mutex_lock(&interface_data.lock);
	if (!interface_data.ops || !interface_data.ops->register_event_notifier) {
		LOGW("No implementation registered for %s\n", __func__);
		mutex_unlock(&interface_data.lock);
		return -EOPNOTSUPP;
	}
	ret = interface_data.ops->register_event_notifier(client_data, callback);
	mutex_unlock(&interface_data.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(register_i2c_arbitration_event_notifier);

void unregister_i2c_arbitration_event_notifier(
	int (*callback)(void *client_data, enum i2c_arbitration_event_type type))
{
	mutex_lock(&interface_data.lock);
	if (!interface_data.ops || !interface_data.ops->unregister_event_notifier) {
		LOGW("No implementation registered for %s\n", __func__);
		mutex_unlock(&interface_data.lock);
		return;
	}
	interface_data.ops->unregister_event_notifier(callback);
	mutex_unlock(&interface_data.lock);
}
EXPORT_SYMBOL_GPL(unregister_i2c_arbitration_event_notifier);

int i2c_arbitration_register_ops(const struct i2c_arbitration_ops *ops)
{
	int ret = 0;

	mutex_lock(&interface_data.lock);
	if (interface_data.ops) {
		LOGE("Implementation already registered\n");
		ret = -EBUSY;
		goto out;
	}
	interface_data.ops = ops;
	LOGI("Implementation registered\n");
out:
	mutex_unlock(&interface_data.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(i2c_arbitration_register_ops);

void i2c_arbitration_unregister_ops(void)
{
	mutex_lock(&interface_data.lock);
	interface_data.ops = NULL;
	LOGI("Implementation unregistered\n");
	mutex_unlock(&interface_data.lock);
}
EXPORT_SYMBOL_GPL(i2c_arbitration_unregister_ops);

static int __init i2c_arbitration_interface_init(void)
{
	mutex_init(&interface_data.lock);
	LOGI("I2C Arbitration Interface initialized\n");
	return 0;
}

static void __exit i2c_arbitration_interface_exit(void)
{
}

module_init(i2c_arbitration_interface_init);
module_exit(i2c_arbitration_interface_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Google Pixel Wear Touch Team");
MODULE_DESCRIPTION("I2C Arbitration Common Interface");
MODULE_SOFTDEP("pre: mcu_transport_autobahn");
