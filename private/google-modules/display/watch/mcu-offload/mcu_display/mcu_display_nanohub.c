// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "mcu_display_nanohub.h"
#include "mcu_display_exports.h"
#include "nanohub_exports.h"

#define NANOHUB_DISPLAY_COMMAND_VERSION	     0x01
#define NANOHUB_DISPLAY_GET_STATE_TIMEOUT_MS 100

static DEFINE_MUTEX(mcu_display_cb_mutex);
static DEFINE_MUTEX(display_state_mutex);
static DECLARE_COMPLETION(display_state_completion);

static struct display_state_data display_state;
static struct display_sync_info_data display_sync_info;

struct display_callback_function {
	Callback cb;
	void *priv_data;
	struct list_head list;
};

static struct list_head display_cb_funcs[NANOHUB_DISPLAY_COMMAND_TYPE_MAX];

static void send_message(u8 cmd, void *msg, size_t msg_size)
{
	void *message;
	struct CommandHeader header = {
		.version = NANOHUB_DISPLAY_COMMAND_VERSION,
		.command = cmd,
	};

	if (msg == NULL && msg_size) {
		pr_err("mcu_display_nanohub: Invalid message\n");
		return;
	}

	message = kmalloc(sizeof(header) + msg_size, GFP_KERNEL);
	if (!message) {
		pr_err("mcu_display_nanohub: Failed to send message, no space\n");
		return;
	}

	memcpy(message, &header, sizeof(header));
	if (msg_size)
		memcpy(message + sizeof(header), msg, msg_size);

	nanohub_send_message(NANOHUB_DISPLAY_KERNEL_CHANNEL_ID, message, sizeof(header) + msg_size);

	kfree(message);
}

static void on_message_received(const char *buffer, size_t length)
{
	u8 command;
	const size_t header_size = sizeof(struct CommandHeader);
	struct display_callback_function *cb_func;
	struct display_callback_data cb_data;

	if (length < header_size || buffer[0] != NANOHUB_DISPLAY_COMMAND_VERSION) {
		pr_err("mcu_display_nanohub: Invalid message received\n");
		return;
	}

	command = buffer[1];
	if (command >= NANOHUB_DISPLAY_COMMAND_TYPE_MAX) {
		pr_err("mcu_display_nanohub: Invalid command type\n");
		return;
	}

	if (length > header_size) {
		cb_data.buf = buffer + header_size;
		cb_data.buf_len = length - header_size;
	} else {
		cb_data.buf = NULL;
		cb_data.buf_len = 0;
	}

	mutex_lock(&mcu_display_cb_mutex);
	list_for_each_entry(cb_func, &display_cb_funcs[command], list) {
		cb_func->cb(&cb_data, cb_func->priv_data, send_message);
	}
	mutex_unlock(&mcu_display_cb_mutex);
}

static struct display_callback_function *mcu_display_get_callback_locked(u8 cmd, Callback cb)
{
	struct display_callback_function *cb_func;

	list_for_each_entry(cb_func, &display_cb_funcs[cmd], list) {
		if (cb_func->cb == cb)
			return cb_func;
	}

	return NULL;
}

static void mcu_display_display_state_callback(struct display_callback_data *data, void *priv_data,
					       Reply reply)
{
	if (data->buf_len == 0 || data->buf_len != sizeof(struct display_state_data))
		return;

	display_state = *(struct display_state_data *)data->buf;
	pr_info("%s: state(%d)\n", __func__, display_state.display_mode);
	complete(&display_state_completion);
}

static void mcu_display_display_sync_info_callback(struct display_callback_data *data,
						   void *priv_data, Reply reply)
{
	if (data->buf_len == 0 || data->buf_len != sizeof(struct display_sync_info_data))
		return;

	display_sync_info = *(struct display_sync_info_data *)data->buf;
	pr_info("%s: state(%d, %d) brightness(%d, %d)\n", __func__, display_sync_info.display_mode,
		display_sync_info.panel_mode, display_sync_info.nbm_brightness,
		display_sync_info.aod_brightness);
	complete(&display_state_completion);
}

static int nanohub_query_display_internal(const int query_command)
{
	long timeout;

	mutex_lock(&display_state_mutex);
	reinit_completion(&display_state_completion);

	send_message(query_command, NULL, 0);
	timeout = wait_for_completion_interruptible_timeout(
		&display_state_completion, msecs_to_jiffies(NANOHUB_DISPLAY_GET_STATE_TIMEOUT_MS));
	mutex_unlock(&display_state_mutex);

	if (timeout <= 0) {
		pr_err("mcu_display: failed to query display state with command %d, ret = %ld\n",
		       query_command, timeout);
		if (timeout == 0)
			return -ETIMEDOUT;
		else
			return timeout;
	}

	return 0;
}

int nanohub_get_display_state(struct display_state_data *data)
{
	int ret;

	if (data == NULL)
		return -EINVAL;

	ret = nanohub_query_display_internal(NANOHUB_DISPLAY_COMMAND_GET_STATE);
	if (ret < 0)
		return ret;

	data->display_mode = display_state.display_mode;
	return ret;
}

int nanohub_sync_display_info(struct display_sync_info_data *data)
{
	int ret;

	if (data == NULL)
		return -EINVAL;

	ret = nanohub_query_display_internal(NANOHUB_DISPLAY_COMMAND_SYNC_INFO);
	if (ret < 0)
		return ret;

	data->display_mode = display_sync_info.display_mode;
	data->panel_mode = display_sync_info.panel_mode;
	data->nbm_brightness = display_sync_info.nbm_brightness;
	data->aod_brightness = display_sync_info.aod_brightness;

	return ret;
}

bool mcu_display_register_nanohub(void)
{
	return nanohub_register_listener(NANOHUB_DISPLAY_KERNEL_CHANNEL_ID, on_message_received);
}

void mcu_display_unregister_nanohub(void)
{
	nanohub_unregister_listener(NANOHUB_DISPLAY_KERNEL_CHANNEL_ID);
}

bool mcu_display_register_listener_internal(u8 cmd, Callback cb, void *priv_data)
{
	struct display_callback_function *cb_func;

	if (cb == NULL) {
		pr_err("%s: Failed to register listener, null cb\n", __func__);
		return false;
	}

	if (cmd >= NANOHUB_DISPLAY_COMMAND_TYPE_MAX) {
		pr_err("%s: Invalid command type\n", __func__);
		return false;
	}

	cb_func = kmalloc(sizeof(struct display_callback_function), GFP_KERNEL);
	if (!cb_func) {
		pr_err("%s: Failed to allocate mem space\n", __func__);
		return false;
	}

	cb_func->cb = cb;
	cb_func->priv_data = priv_data;

	mutex_lock(&mcu_display_cb_mutex);
	if (mcu_display_get_callback_locked(cmd, cb) == NULL) {
		list_add(&cb_func->list, &display_cb_funcs[cmd]);
	} else {
		pr_info("%s: Callback already exists\n", __func__);
		kfree(cb_func);
	}
	mutex_unlock(&mcu_display_cb_mutex);

	return true;
}

void mcu_display_unregister_listener_internal(u8 cmd, Callback cb)
{
	struct display_callback_function *cb_func;

	if (cb == NULL) {
		pr_err("mcu_display_nanohub: Failed to unregister listener, null cb\n");
		return;
	}

	if (cmd >= NANOHUB_DISPLAY_COMMAND_TYPE_MAX) {
		pr_err("%s:  Invalid command type\n", __func__);
		return;
	}

	mutex_lock(&mcu_display_cb_mutex);
	cb_func = mcu_display_get_callback_locked(cmd, cb);
	if (cb_func) {
		list_del(&cb_func->list);
		kfree(cb_func);
	}
	mutex_unlock(&mcu_display_cb_mutex);
}

bool mcu_display_init_internal(void)
{
	u8 cmd = 0;
	for (cmd = 0; cmd < NANOHUB_DISPLAY_COMMAND_TYPE_MAX; cmd++)
		INIT_LIST_HEAD(&display_cb_funcs[cmd]);

	/* Register internal listener for events */
	if (!mcu_display_register_listener_internal(NANOHUB_DISPLAY_COMMAND_GET_STATE,
						    mcu_display_display_state_callback, NULL))
		goto error;
	if (!mcu_display_register_listener_internal(NANOHUB_DISPLAY_COMMAND_SYNC_INFO,
						    mcu_display_display_sync_info_callback, NULL))
		goto error;

	return true;
error:
	mcu_display_exit_internal();
	return false;
}

void mcu_display_exit_internal(void)
{
	u8 cmd = 0;
	struct display_callback_function *cb_func, *tmp;
	for (cmd = 0; cmd < NANOHUB_DISPLAY_COMMAND_TYPE_MAX; cmd++) {
		list_for_each_entry_safe(cb_func, tmp, &display_cb_funcs[cmd], list) {
			list_del(&cb_func->list);
			kfree(cb_func);
		}
	}
}
