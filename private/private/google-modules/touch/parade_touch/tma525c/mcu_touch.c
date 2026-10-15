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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include "nanohub_exports.h"
#include "mcu_touch.h"
#include <asm-generic/errno.h>

#define NANOHUB_TOUCH_COMMAND_VERSION		     0x01
#define NANOHUB_TOUCH_MESSAGE_SIZE		     3
#define NANOHUB_TOUCH_MESSAGE_COMMAND_VERSION_OFFSET 0
#define NANOHUB_TOUCH_MESSAGE_COMMAND_TYPE_OFFSET    1
#define NANOHUB_TOUCH_MESSAGE_DISPLAY_MODE_OFFSET    2

struct nanohub_touch_callback {
	void (*on_message_received)(const struct touch_callback_data *data);
	struct list_head list;
};

struct nanohub_touch_host {
	struct mutex nanohub_touch_mutex;
	bool initialized;
	struct list_head touch_callbacks[NANOHUB_TOUCH_COMMAND_TYPE_MAX];
};

static struct nanohub_touch_host touch_host = {
	.nanohub_touch_mutex = __MUTEX_INITIALIZER(touch_host.nanohub_touch_mutex),
};

static void parse_callback_data(struct touch_callback_data *callback_data, const char *buffer,
				size_t length)
{
	uint8_t command_type;

	if (length != NANOHUB_TOUCH_MESSAGE_SIZE) {
		pr_err("nanohub: the size of touch message from MCU is not matched");
		return;
	}

	command_type = buffer[NANOHUB_TOUCH_MESSAGE_COMMAND_TYPE_OFFSET];

	switch (command_type) {
	case NANOHUB_TOUCH_COMMAND_LISTEN_ON_DISPLAY_STATE:
		callback_data->display_mode = buffer[NANOHUB_TOUCH_MESSAGE_DISPLAY_MODE_OFFSET];
		break;
	default:
		break;
	}
}

static void dispatcher(const char *buffer, size_t length)
{
	uint8_t command_type;
	struct nanohub_touch_callback *touch_callback;
	struct touch_callback_data callback_data;

	pr_info("nanohub : touch host receive message from mcu\n");
	mutex_lock(&touch_host.nanohub_touch_mutex);
	if (length == NANOHUB_TOUCH_MESSAGE_SIZE &&
	    buffer[NANOHUB_TOUCH_MESSAGE_COMMAND_VERSION_OFFSET] == NANOHUB_TOUCH_COMMAND_VERSION) {
		command_type = buffer[NANOHUB_TOUCH_MESSAGE_COMMAND_TYPE_OFFSET];
		parse_callback_data(&callback_data, buffer, length);
		list_for_each_entry(touch_callback, &touch_host.touch_callbacks[command_type],
				    list) {
			touch_callback->on_message_received(&callback_data);
		}
	}
	mutex_unlock(&touch_host.nanohub_touch_mutex);
}

static struct nanohub_touch_callback *
callback_is_existed_locked(void (*on_message_received)(const struct touch_callback_data *data),
			   uint32_t nanohub_touch_command_type)
{
	struct nanohub_touch_callback *touch_callback;

	list_for_each_entry(touch_callback, &touch_host.touch_callbacks[nanohub_touch_command_type],
			    list) {
		if (touch_callback->on_message_received == on_message_received)
			return touch_callback;
	}
	return NULL;
}

static int add_callback_locked(void (*on_message_received)(const struct touch_callback_data *data),
			       uint32_t nanohub_touch_command_type)
{
	struct nanohub_touch_callback *touch_callback;

	if (on_message_received == NULL) {
		pr_err("nanohub: trying to register an NULL callback on command type:%u\n",
		       nanohub_touch_command_type);
		return -EPERM;
	}

	if (callback_is_existed_locked(on_message_received, nanohub_touch_command_type)) {
		pr_err("nanohub: the callback is already existed on command type:%u\n",
			nanohub_touch_command_type);
		return -EPERM;
	}

	touch_callback = kmalloc(sizeof(struct nanohub_touch_callback), GFP_KERNEL);
	if (touch_callback == NULL) {
		pr_err("nanohub: failed to allocate memory on command type:%u\n",
		       nanohub_touch_command_type);
		return -ENOMEM;
	}
	touch_callback->on_message_received = on_message_received;
	list_add(&touch_callback->list, &touch_host.touch_callbacks[nanohub_touch_command_type]);
	pr_info("nanohub : touch callback is being registered on command type:%u\n",
		nanohub_touch_command_type);
	return 0;
}

static int
remove_callback_locked(void (*on_message_received)(const struct touch_callback_data *data),
		       uint32_t nanohub_touch_command_type)
{
	struct nanohub_touch_callback *touch_callback;

	touch_callback =
		callback_is_existed_locked(on_message_received, nanohub_touch_command_type);
	if (!touch_callback) {
		pr_err("nanohub: the callback is not existed on command type:%u\n",
		       nanohub_touch_command_type);
		return -EINVAL;
	}

	pr_info("nanohub : touch callback is being unregistered from command type:%u\n",
		nanohub_touch_command_type);
	list_del(&touch_callback->list);
	kfree(touch_callback);
	return 0;
}

static void touch_host_initialize_locked(void)
{
	uint32_t command_type;

	pr_info("nanohub: initializing touch host\n");
	for (command_type = 0; command_type < NANOHUB_TOUCH_COMMAND_TYPE_MAX; command_type++)
		INIT_LIST_HEAD(&touch_host.touch_callbacks[command_type]);
	nanohub_register_listener(NANOHUB_TOUCH_KERNEL_CHANNEL_ID, dispatcher);
	touch_host.initialized = true;
}

void nanohub_touch_register_listener(
	void (*on_message_received)(const struct touch_callback_data *data),
	uint32_t nanohub_touch_command_type)
{
	int rc;

	mutex_lock(&touch_host.nanohub_touch_mutex);
	if (!touch_host.initialized)
		touch_host_initialize_locked();

	rc = add_callback_locked(on_message_received, nanohub_touch_command_type);
	mutex_unlock(&touch_host.nanohub_touch_mutex);
	if (rc)
		pr_err("nanohub: Failed to register listener on command type:%u, rc=%d\n",
		       nanohub_touch_command_type, rc);
}
EXPORT_SYMBOL(nanohub_touch_register_listener);

void nanohub_touch_unregister_listener(
	void (*on_message_received)(const struct touch_callback_data *data),
	uint32_t nanohub_touch_command_type)
{
	int rc;

	if (on_message_received == NULL) {
		pr_err("nanohub: trying to unregister an NULL callback on command type:%u\n",
		       nanohub_touch_command_type);
		return;
	}
	mutex_lock(&touch_host.nanohub_touch_mutex);
	rc = remove_callback_locked(on_message_received, nanohub_touch_command_type);
	mutex_unlock(&touch_host.nanohub_touch_mutex);
	if (rc)
		pr_err("nanohub: Failed to unregister listener on command type:%u, rc=%d\n",
		       nanohub_touch_command_type, rc);
}
EXPORT_SYMBOL(nanohub_touch_unregister_listener);
