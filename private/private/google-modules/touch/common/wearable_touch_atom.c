// SPDX-License-Identifier: GPL-2.0
/*
 * Wear Touch Telemetry Implementation for Pixel wearable devices.
 *
 * Copyright 2026 Google LLC.
 */

#define pr_fmt(fmt) "[wti-atom]" fmt

#include <asm-generic/errno.h>
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "wearable_touch_atom.h"
#include "wearable_touch_interface.h"
#include "wearable_touch_log.h"

/* Helper function for reporting atoms with 2 int fields */
static ssize_t wti_atom_report_and_reset_int_int(enum wti_atom atom_id, atomic_t *data,
						 size_t length, char *buffer, ssize_t size)
{
	int offset = 0, count = 0;

	for (int i = 0; i < length; i++) {
		count = atomic_xchg(&data[i], 0);
		if (count == 0)
			continue;

		offset += scnprintf(buffer + offset, size - offset, "{%d, %d, %d}\n", atom_id, i,
				    count);
	}

	return offset;
}

int wti_atom_error_count_append(struct wti_atom_data *atom_data,
				enum wti_atom_error_type error_type)
{
	if (error_type >= WTI_ATOM_ERROR_MAX) {
		WTI_LOGE("%s: unknown error type %d\n", __func__, error_type);
		return -EINVAL;
	}

	atomic_inc(&atom_data->error_count_data[error_type]);

	return 0;
}
EXPORT_SYMBOL_GPL(wti_atom_error_count_append);

ssize_t wti_atom_error_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
					      ssize_t size)
{
	return wti_atom_report_and_reset_int_int(WTI_ATOM_ERROR_COUNT, atom_data->error_count_data,
						 ARRAY_SIZE(atom_data->error_count_data), buffer,
						 size);
}

int wti_atom_gesture_count_append(struct wti_atom_data *atom_data,
				  enum wti_atom_gesture_type gesture_type)
{
	if (gesture_type >= WTI_ATOM_GESTURE_MAX) {
		WTI_LOGE("%s: unknown gesture type %d\n", __func__, gesture_type);
		return -EINVAL;
	}

	atomic_inc(&atom_data->gesture_count_data[gesture_type]);

	return 0;
}
EXPORT_SYMBOL_GPL(wti_atom_gesture_count_append);

ssize_t wti_atom_gesture_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
						ssize_t size)
{
	return wti_atom_report_and_reset_int_int(WTI_ATOM_GESTURE_COUNT,
						 atom_data->gesture_count_data,
						 ARRAY_SIZE(atom_data->gesture_count_data), buffer,
						 size);
}

int wti_atom_frame_count_append(struct wti_atom_data *atom_data,
				enum wti_atom_frame_event_type event_type)
{
	if (event_type >= WTI_ATOM_FRAME_EVENT_MAX) {
		WTI_LOGE("%s: unknown event type %d\n", __func__, event_type);
		return -EINVAL;
	}

	atomic_inc(&atom_data->frame_count_data[event_type]);

	return 0;
}
EXPORT_SYMBOL_GPL(wti_atom_frame_count_append);

ssize_t wti_atom_frame_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
					      ssize_t size)
{
	return wti_atom_report_and_reset_int_int(WTI_ATOM_FRAME_COUNT, atom_data->frame_count_data,
						 ARRAY_SIZE(atom_data->frame_count_data), buffer,
						 size);
}

int wti_atom_timeout_count_append(struct wti_atom_data *atom_data,
				  enum wti_atom_timeout_event_type event_type)
{
	if (event_type >= WTI_ATOM_TIMEOUT_EVENT_MAX) {
		WTI_LOGE("%s: unknown event type %d\n", __func__, event_type);
		return -EINVAL;
	}

	atomic_inc(&atom_data->timeout_count_data[event_type]);

	return 0;
}
EXPORT_SYMBOL_GPL(wti_atom_timeout_count_append);

ssize_t wti_atom_timeout_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
						ssize_t size)
{
	return wti_atom_report_and_reset_int_int(WTI_ATOM_TIMEOUT_COUNT,
						 atom_data->timeout_count_data,
						 ARRAY_SIZE(atom_data->timeout_count_data), buffer,
						 size);
}

int wti_atom_offload_latency_append(struct wti_atom_data *atom_data,
				    enum wti_atom_latency_type latency_type, uint16_t latency)
{
	int ret = 0;
	struct wti_atom_offload_latency_data *data = &atom_data->offload_latency_data;
	struct wti_atom_offload_latency_entry *entry;
	struct wearable_touch_interface *wti =
		container_of(atom_data, struct wearable_touch_interface, atom_data);

	if (latency_type >= WTI_ATOM_LATENCY_MAX) {
		WTI_LOGE("%s: unknown latency type %d\n", __func__, latency_type);
		return -EINVAL;
	}

	entry = devm_kzalloc(wti->vendor_dev, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->type = latency_type;
	entry->value = latency;

	mutex_lock(&data->lock);
	list_add_tail(&entry->list, &data->latency_list);
	mutex_unlock(&data->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wti_atom_offload_latency_append);

ssize_t wti_atom_offload_latency_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
						  ssize_t size)
{
	int offset = 0;
	struct wti_atom_offload_latency_data *data = &atom_data->offload_latency_data;
	struct wti_atom_offload_latency_entry *entry, *tmp;
	struct wearable_touch_interface *wti =
		container_of(atom_data, struct wearable_touch_interface, atom_data);

	mutex_lock(&data->lock);
	list_for_each_entry_safe(entry, tmp, &data->latency_list, list) {
		offset += scnprintf(buffer + offset, size - offset, "{%d, %d, %d}\n",
				    WTI_ATOM_OFFLOAD_LATENCY, entry->type, entry->value);
		list_del(&entry->list);
		devm_kfree(wti->vendor_dev, entry);
	}
	mutex_unlock(&data->lock);

	return offset;
}
