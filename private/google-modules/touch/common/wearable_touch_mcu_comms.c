/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Comms implementation of Wear Touch MCU Interface for Pixel wearable devices.
 *
 * Copyright 2024 Google LLC.
 */
#include <asm-generic/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "mcu_transport_interface.h"
#include "wearable_touch_log.h"
#include "wearable_touch_mcu.h"

#define MCU_TOUCH_COMMAND_VERSION 0x01

struct mcu_cmd_header {
	uint8_t version;
	uint8_t command;
} __packed;

struct mcu_input_event {
	uint8_t type;
	uint32_t code;
	int32_t value;
} __packed;

struct wti_mcu_touch_host {
	struct mutex mcu_touch_mutex;
	bool initialized;
	struct list_head touch_callbacks[MCU_TOUCH_COMMAND_TYPE_MAX];
	enum mcu_touch_format_type format;
};

static struct wti_mcu_touch_host wti_mcu_touch_host = {
	.mcu_touch_mutex = __MUTEX_INITIALIZER(wti_mcu_touch_host.mcu_touch_mutex),
	/* Default to use the format for common Touch Service */
	.format = MCU_TOUCH_DATA_ONLY_FORMAT,
};

struct mcu_touch_callback {
	void (*on_message_received)(const struct mcu_touch_callback_data *data);
	struct list_head list;
};

struct mcu_input_message {
	struct mcu_cmd_header header;
	struct mcu_input_event event;
} __packed;

struct wearable_touch_ap_settings_message {
	struct mcu_cmd_header header;
	struct wearable_touch_ap_settings settings;
} __packed;

static inline void assign_header(uint8_t version, uint8_t cmd, struct mcu_cmd_header *header)
{
	if (unlikely(header == NULL))
		return;

	memcpy(&header->version, &version, sizeof(version));
	memcpy(&header->command, &cmd, sizeof(cmd));
}

static inline void assign_input_event(uint8_t type, uint32_t code, int32_t value,
				      struct mcu_input_event *input)
{
	if (unlikely(input == NULL))
		return;

	memcpy(&input->type, &type, sizeof(input->type));
	memcpy(&input->code, &code, sizeof(input->code));
	memcpy(&input->value, &value, sizeof(input->value));
}

static inline void assign_input_message(uint8_t type, uint32_t code, int32_t value,
					struct mcu_input_message *input)
{
	if (unlikely(input == NULL))
		return;

	assign_header(MCU_TOUCH_COMMAND_VERSION, MCU_TOUCH_COMMAND_INPUT_EVENT, &input->header);
	assign_input_event(type, code, value, &input->event);
}

static inline void assign_offload_message(bool enable, struct mcu_cmd_header *cmd_msg)
{
	if (unlikely(cmd_msg == NULL))
		return;

	cmd_msg->version = MCU_TOUCH_COMMAND_VERSION;
	cmd_msg->command = enable ? MCU_TOUCH_COMMAND_ENABLE_OFFLOAD :
				    MCU_TOUCH_COMMAND_DISABLE_OFFLOAD;
}

static inline void assign_settings_message(struct wearable_touch_ap_settings *settings,
					   struct wearable_touch_ap_settings_message *msg)
{
	if (unlikely(msg == NULL))
		return;

	msg->header.version = MCU_TOUCH_COMMAND_VERSION;
	msg->header.command = MCU_TOUCH_COMMAND_AP_SYNC_INFORMATION_TO_MCU;

	memcpy(&msg->settings, settings, sizeof(struct wearable_touch_ap_settings));
}

static int wti_send_mcu_message(unsigned int type, unsigned int code, int value)
{
	ssize_t bytes;
	struct mcu_input_message input_msg;
	const void *data;
	size_t size;

	assign_input_message(type, code, value, &input_msg);

	if (wti_mcu_touch_host.format == MCU_TOUCH_DATA_ONLY_FORMAT) {
		// Send the data without header for Touch Service
		data = &input_msg.event;
		size = sizeof(input_msg.event);
	} else {
		// Send the data with header for Touch Offload Service
		data = &input_msg;
		size = sizeof(input_msg);
	}

	bytes = mcu_transport_send_msg(MCU_TRANSPORT_TOUCH_CHANNEL_ID, (const char *)data, size);
	if (bytes != size) {
		WTI_LOGE("MCU Bytes sent expected = %zd, actual = %zd\n", size, bytes);
		return -EIO;
	}
	return 0;
}

static inline void wti_mcu_report_key(unsigned int code, int value)
{
	wti_send_mcu_message(EV_KEY, code, value);
}

static inline void wti_mcu_report_abs(unsigned int code, int value)
{
	wti_send_mcu_message(EV_ABS, code, value);
}

static inline void wti_mcu_sync(void)
{
	wti_send_mcu_message(EV_SYN, SYN_REPORT, 0);
}

void wti_mcu_send_key(uint32_t keycode)
{
	wti_mcu_report_key(keycode, 1);
	wti_mcu_sync();
	wti_mcu_report_key(keycode, 0);
	wti_mcu_sync();
}
EXPORT_SYMBOL_GPL(wti_mcu_send_key);

static int parse_callback_data(struct mcu_touch_callback_data *callback_data,
			       struct mcu_cmd_header *header, const char *buffer, size_t length)
{
	ssize_t msg_body_size;

	if (length < sizeof(struct mcu_cmd_header)) {
		WTI_LOGE("Received unexpected message size:%zu\n", length);
		return -EBADMSG;
	}

	memcpy(header, buffer, sizeof(struct mcu_cmd_header));
	if (header->version != MCU_TOUCH_COMMAND_VERSION) {
		WTI_LOGE("Unsupported command version : %u\n", header->version);
		return -EINVAL;
	}

	msg_body_size = length - sizeof(struct mcu_cmd_header);
	switch (header->command) {
	case MCU_TOUCH_COMMAND_GET_DISPLAY_STATE:
		if (msg_body_size != sizeof(struct mcu_touch_display_state_callback_data)) {
			WTI_LOGE("receive unmatched display state message: size=%zd",
				 msg_body_size);
			return -EBADMSG;
		}
		memcpy(&callback_data->display_data, &buffer[sizeof(struct mcu_cmd_header)],
		       sizeof(struct mcu_touch_display_state_callback_data));
		break;
	case MCU_TOUCH_COMMAND_POINT_DATA:
		if (msg_body_size != sizeof(struct mcu_touch_input_event_callback_data)) {
			WTI_LOGE("receive unmatched size of input events, size=%zd", msg_body_size);
			return -EBADMSG;
		}
		memcpy(&callback_data->input_event_data, &buffer[sizeof(struct mcu_cmd_header)],
		       sizeof(struct mcu_touch_input_event_callback_data));
		break;
	case MCU_TOUCH_COMMAND_MCU_REQUEST_CONTROL:
		// This command type doesn't contain the message body, no need to check the size
		callback_data->offload_data.enable = true;
		break;
	case MCU_TOUCH_COMMAND_AP_SYNC_INFORMATION_TO_MCU:
		// Get the request from MCU to sync the info
		WTI_LOGI("Receive the request of synchronizing the user settings");
		wti_synchronizer_on_sync();
		break;
	default:
		WTI_LOGE("receive unsupporeted event");
		break;
	}

	return 0;
}

static void wti_mcu_dispatcher(const char *buffer, size_t length)
{
	struct mcu_touch_callback *touch_callback;
	struct mcu_touch_callback_data callback_data;
	struct mcu_cmd_header header;
	int ret;

	/* Only debug message to avoid too dummpy logs */
	WTI_LOGD("touch host receive message from mcu\n");

	ret = parse_callback_data(&callback_data, &header, buffer, length);
	if (ret < 0) {
		WTI_LOGE("data is not correct to be parsed\n");
		return;
	}

	/* Dispatch the data to the listener */
	mutex_lock(&wti_mcu_touch_host.mcu_touch_mutex);
	list_for_each_entry(touch_callback, &wti_mcu_touch_host.touch_callbacks[header.command],
			    list) {
		touch_callback->on_message_received(&callback_data);
	}
	mutex_unlock(&wti_mcu_touch_host.mcu_touch_mutex);
}

static struct mcu_touch_callback *
callback_is_existed_locked(void (*on_message_received)(const struct mcu_touch_callback_data *data),
			   uint32_t mcu_touch_command_type)
{
	struct mcu_touch_callback *touch_callback;

	list_for_each_entry(touch_callback,
			    &wti_mcu_touch_host.touch_callbacks[mcu_touch_command_type], list) {
		if (touch_callback->on_message_received == on_message_received)
			return touch_callback;
	}
	return NULL;
}

static int
add_callback_locked(void (*on_message_received)(const struct mcu_touch_callback_data *data),
		    uint32_t mcu_touch_command_type)
{
	struct mcu_touch_callback *touch_callback;

	if (on_message_received == NULL) {
		WTI_LOGE("trying to register an NULL callback on command type:%u\n",
			 mcu_touch_command_type);
		return -EPERM;
	}

	if (callback_is_existed_locked(on_message_received, mcu_touch_command_type)) {
		WTI_LOGE("the callback is already existed on command type:%u\n",
			 mcu_touch_command_type);
		return -EPERM;
	}

	touch_callback = kmalloc(sizeof(struct mcu_touch_callback), GFP_KERNEL);
	if (touch_callback == NULL) {
		WTI_LOGE("failed to allocate memory on command type:%u\n", mcu_touch_command_type);
		return -ENOMEM;
	}
	touch_callback->on_message_received = on_message_received;
	list_add(&touch_callback->list,
		 &wti_mcu_touch_host.touch_callbacks[mcu_touch_command_type]);
	WTI_LOGI("touch callback is being registered on command type:%u\n", mcu_touch_command_type);
	return 0;
}
static int
remove_callback_locked(void (*on_message_received)(const struct mcu_touch_callback_data *data),
		       uint32_t mcu_touch_command_type)
{
	struct mcu_touch_callback *touch_callback;

	touch_callback = callback_is_existed_locked(on_message_received, mcu_touch_command_type);
	if (!touch_callback) {
		WTI_LOGE("the callback is not existed on command type:%u\n",
			 mcu_touch_command_type);
		return -EINVAL;
	}

	WTI_LOGI("touch callback is being unregistered from command type:%u\n",
		 mcu_touch_command_type);
	list_del(&touch_callback->list);
	kfree(touch_callback);
	return 0;
}

static void wti_mcu_touch_host_initialize_locked(void)
{
	uint32_t command_type;

	WTI_LOGI("initializing touch host\n");
	for (command_type = 0; command_type < MCU_TOUCH_COMMAND_TYPE_MAX; command_type++)
		INIT_LIST_HEAD(&wti_mcu_touch_host.touch_callbacks[command_type]);
	wti_mcu_touch_host.initialized = true;
}

bool mcu_touch_register_dispatcher(void)
{
	return !mcu_transport_register_channel_listener(MCU_TRANSPORT_TOUCH_CHANNEL_ID,
							wti_mcu_dispatcher);
}
EXPORT_SYMBOL(mcu_touch_register_dispatcher);

void mcu_touch_register_listener(
	void (*on_message_received)(const struct mcu_touch_callback_data *data),
	uint32_t mcu_touch_command_type)
{
	int rc;

	mutex_lock(&wti_mcu_touch_host.mcu_touch_mutex);
	if (!wti_mcu_touch_host.initialized)
		wti_mcu_touch_host_initialize_locked();

	rc = add_callback_locked(on_message_received, mcu_touch_command_type);
	mutex_unlock(&wti_mcu_touch_host.mcu_touch_mutex);
	if (rc)
		WTI_LOGE("Failed to register listener on command type:%u, rc=%d\n",
			 mcu_touch_command_type, rc);
}
EXPORT_SYMBOL(mcu_touch_register_listener);

void mcu_touch_unregister_listener(
	void (*on_message_received)(const struct mcu_touch_callback_data *data),
	uint32_t mcu_touch_command_type)
{
	int rc;

	if (on_message_received == NULL) {
		WTI_LOGE("trying to unregister an NULL callback on command type:%u\n",
			 mcu_touch_command_type);
		return;
	}
	mutex_lock(&wti_mcu_touch_host.mcu_touch_mutex);
	rc = remove_callback_locked(on_message_received, mcu_touch_command_type);
	mutex_unlock(&wti_mcu_touch_host.mcu_touch_mutex);

	if (rc)
		WTI_LOGE("Failed to unregister listener on command type:%u, rc=%d\n",
			 mcu_touch_command_type, rc);
}
EXPORT_SYMBOL(mcu_touch_unregister_listener);

void wti_mcu_send_offload_settings(bool enable)
{
	ssize_t bytes;
	struct mcu_cmd_header cmd_msg;

	assign_offload_message(enable, &cmd_msg);

	bytes = mcu_transport_send_msg(MCU_TRANSPORT_TOUCH_CHANNEL_ID, (const char *)&cmd_msg,
				       sizeof(cmd_msg));
	if (bytes != sizeof(cmd_msg))
		WTI_LOGE("MCU Bytes sent expected = %zd, actual = %zd\n", sizeof(cmd_msg), bytes);
}
EXPORT_SYMBOL_GPL(wti_mcu_send_offload_settings);

void wti_mcu_set_message_format(enum mcu_touch_format_type format_type)
{
	WTI_LOGI("Set to %d message format to communicate with MCU\n", format_type);
	wti_mcu_touch_host.format = format_type;
}
EXPORT_SYMBOL_GPL(wti_mcu_set_message_format);

void wti_mcu_send_settings(struct wearable_touch_ap_settings *settings)
{
	ssize_t bytes;
	struct wearable_touch_ap_settings_message msg;

	assign_settings_message(settings, &msg);

	bytes = mcu_transport_send_msg(MCU_TRANSPORT_TOUCH_CHANNEL_ID, (const char *)&msg,
				       sizeof(msg));
	if (bytes != sizeof(msg))
		WTI_LOGE("MCU Bytes sent expected = %zd, actual = %zd\n", sizeof(msg), bytes);
}
EXPORT_SYMBOL_GPL(wti_mcu_send_settings);
