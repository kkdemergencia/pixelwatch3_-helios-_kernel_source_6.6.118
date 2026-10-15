/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wear Touch Interface for Pixel wearable devices.
 *
 * Copyright 2024 Google LLC.
 */

#ifndef _WEARABLE_TOUCH_INTERFACE_H_
#define _WEARABLE_TOUCH_INTERFACE_H_

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include "linux/netdevice.h"
#include "linux/types.h"
#include "wearable_touch_data_capturer.h"
#include "wearable_touch_atom.h"

#define MAX_FINGERS 2
#define MAX_HW_CONFIG_SIZE 100
#define MAX_FW_CONFIG_SIZE 100

// Used to generate a node in /proc
#define TOUCHSTATS_NAME "touchstats"

enum log_event_type {
	GESTURE,
	MOTION,
};

enum log_gesture_type {
	TAP_TO_WAKE,
	SLIDE_UP_TO_WAKE,
	SLIDE_DOWN_TO_WAKE,
	SLIDE_LEFT_TO_WAKE,
	SLIDE_RIGHT_TO_WAKE,
	PALM,
	GESTURE_TYPE_MAX,
};

struct log_event {
	enum log_event_type type;
	struct timespec64 ts_bgn;
	struct timespec64 ts_end;
	uint32_t count;
	uint32_t finger_count[MAX_FINGERS];
	union {
		enum log_gesture_type gesture;
		struct {
			int32_t last_x;
			int32_t last_y;
			int32_t sum_dx;
			int32_t sum_dy;
			uint32_t sum_abs_dx;
			uint32_t sum_abs_dy;
		} motion;
	} data;
};

struct wti_fw_version {
	char buffer[MAX_FW_CONFIG_SIZE];
};
static_assert(sizeof(struct wti_fw_version) < PAGE_SIZE);

struct wti_hardware_identifier {
	char buffer[MAX_HW_CONFIG_SIZE];
};
static_assert(sizeof(struct wti_hardware_identifier) < PAGE_SIZE);

struct wti_common_apis {
	int (*get_fw_version)(void *private_data, struct wti_fw_version *fw_ver);
	int (*get_display_resolution)(void *private_data,
				      struct display_resolution_pixels *disp_resolution);
	int (*get_hardware_identifier)(void *private_data, struct wti_hardware_identifier *hw_id);
	int (*get_grid_spec)(void *private_data, enum TOUCH_GRID_SPEC *grid_spec);
};

/* Atom data for atom access */
struct wti_atom_data {
	atomic_t error_count_data[WTI_ATOM_ERROR_MAX];
	atomic_t gesture_count_data[WTI_ATOM_GESTURE_MAX];
	atomic_t frame_count_data[WTI_ATOM_FRAME_EVENT_MAX];
	atomic_t timeout_count_data[WTI_ATOM_TIMEOUT_EVENT_MAX];
	struct wti_atom_offload_latency_data offload_latency_data;
};

struct wearable_touch_interface {
	void *vendor_private_data;
	struct device *vendor_dev;
	struct input_dev *vendor_input_dev;
	struct device *dev;
	dev_t dev_id;
	struct wti_common_apis common_apis;
	struct touch_data_capturer capturer;
	struct wti_atom_data atom_data;
};

extern struct wearable_touch_interface *wti_probe(
	void *private_data,
	struct device *dev,
	struct input_dev *input_dev,
	struct wti_common_apis *apis);
extern int wti_remove(struct wearable_touch_interface *wti);

void wti_log_add_gesture(enum log_gesture_type gesture, ktime_t timestamp);
void wti_log_add_motion(uint32_t x, uint32_t y, uint8_t fingers, ktime_t timestamp);
void wti_log_close_motion(void);
void wti_log_reset(void);
ssize_t wti_dump_touchstats(char *buf, uint32_t max_size);

int wti_touchstats_init(void);
void wti_touchstats_deinit(void);

/* APIs for read or write atoms */
int wti_atom_error_count_append(struct wti_atom_data *atom_data,
				enum wti_atom_error_type error_type);
ssize_t wti_atom_error_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
					      ssize_t size);
int wti_atom_gesture_count_append(struct wti_atom_data *atom_data,
				  enum wti_atom_gesture_type gesture_type);
ssize_t wti_atom_gesture_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
						ssize_t size);
int wti_atom_frame_count_append(struct wti_atom_data *atom_data,
				enum wti_atom_frame_event_type event_type);
ssize_t wti_atom_frame_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
					      ssize_t size);
int wti_atom_timeout_count_append(struct wti_atom_data *atom_data,
				  enum wti_atom_timeout_event_type event_type);
ssize_t wti_atom_timeout_count_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
						ssize_t size);
int wti_atom_offload_latency_append(struct wti_atom_data *atom_data,
				    enum wti_atom_latency_type latency_type, uint16_t latency);
ssize_t wti_atom_offload_latency_report_and_reset(struct wti_atom_data *atom_data, char *buffer,
						  ssize_t size);

#endif // _WEARABLE_TOUCH_INTERFACE_H_
