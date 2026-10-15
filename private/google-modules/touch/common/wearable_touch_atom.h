/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wear Touch Telemetry Interface for Pixel wearable devices.
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _WEARABLE_TOUCH_ATOM_H_
#define _WEARABLE_TOUCH_ATOM_H_

#include <linux/list.h>
#include <linux/mutex.h>

/* Enum for all the atoms */
enum wti_atom {
	WTI_ATOM_ERROR_COUNT = 0,
	WTI_ATOM_GESTURE_COUNT,
	WTI_ATOM_FRAME_COUNT,
	WTI_ATOM_TIMEOUT_COUNT,
	WTI_ATOM_OFFLOAD_LATENCY,

	/* Leave it the last element */
	WTI_ATOM_MAX,
};

/* Define the data of the atom here if needed */
enum wti_atom_error_type {
	WTI_ATOM_ERROR_UNKNOWN = 0,
	WTI_ATOM_ERROR_I2C_RESUME,
	WTI_ATOM_ERROR_I2C_IO,
	WTI_ATOM_ERROR_RESET,
	WTI_ATOM_ERROR_FINGER_COUNT,
	WTI_ATOM_ERROR_DUPLICATE_SEQ,
	WTI_ATOM_ERROR_NFC_SUPPRESS,
	WTI_ATOM_ERROR_MISSING_FRAME,
	WTI_ATOM_ERROR_MAX
};

enum wti_atom_gesture_type {
	WTI_ATOM_GESTURE_UNKNOWN = 0,
	WTI_ATOM_GESTURE_PALM_TO_SLEEP,
	WTI_ATOM_GESTURE_SWIPE_UP,
	WTI_ATOM_GESTURE_SWIPE_DOWN,
	WTI_ATOM_GESTURE_SWIPE_LEFT,
	WTI_ATOM_GESTURE_SWIPE_RIGHT,
	WTI_ATOM_GESTURE_TAP_TO_WAKE,
	WTI_ATOM_GESTURE_MAX
};

enum wti_atom_frame_event_type {
	WTI_ATOM_FRAME_EVENT_UNKNOWN = 0,
	WTI_ATOM_FRAME_EVENT_PALM_TO_SLEEP,
	WTI_ATOM_FRAME_EVENT_SWIPE_UP,
	WTI_ATOM_FRAME_EVENT_SWIPE_DOWN,
	WTI_ATOM_FRAME_EVENT_SWIPE_LEFT,
	WTI_ATOM_FRAME_EVENT_SWIPE_RIGHT,
	WTI_ATOM_FRAME_EVENT_TAP_TO_WAKE,
	WTI_ATOM_FRAME_EVENT_MOTION,
	WTI_ATOM_FRAME_EVENT_MAX
};

enum wti_atom_timeout_event_type {
	WTI_ATOM_TIMEOUT_EVENT_UNKNOWN = 0,
	WTI_ATOM_TIMEOUT_EVENT_PALM_TO_SLEEP,
	WTI_ATOM_TIMEOUT_EVENT_SWIPE_UP,
	WTI_ATOM_TIMEOUT_EVENT_SWIPE_DOWN,
	WTI_ATOM_TIMEOUT_EVENT_SWIPE_LEFT,
	WTI_ATOM_TIMEOUT_EVENT_SWIPE_RIGHT,
	WTI_ATOM_TIMEOUT_EVENT_TAP_TO_WAKE,
	WTI_ATOM_TIMEOUT_EVENT_NORMAL_MOTION,
	WTI_ATOM_TIMEOUT_EVENT_PETC_MOTION,
	WTI_ATOM_TIMEOUT_EVENT_MAX
};

/*
 * Latency of switching from AP to MCU or MCU to AP
 *
 * Note that some latency types are deprecated since they should be recorded
 * from the MCU side.
 *
 * TODO(b/495877008): Adding the latency type for normal touch events
 */
enum wti_atom_latency_type {
	WTI_ATOM_LATENCY_UNKNOWN = 0,

	// Latency of AP takeover request initiation to MCU acknowledgment
	WTI_ATOM_LATENCY_AP_TAKEOVER,

	// Latency of MCU trying to stop touch and RSB services, deprecated since it
	// should be reported from MCU to the userspace directly with the new MCU-side atom
	WTI_ATOM_LATENCY_MCU_PREPARE_TO_HANDOVER
	__attribute__((deprecated("Superseded by new MCU-side atom"))),

	// Latency of AP initiated MCU health check process before offload
	WTI_ATOM_LATENCY_AP_HANDOVER_PRECOND_CHECK,

	// Latency of AP trying to stop touch and RSB drivers
	WTI_ATOM_LATENCY_AP_PREPARE_TO_HANDOVER,

	// Latency of AP handover request initiation to MCU acknowledgment
	WTI_ATOM_LATENCY_AP_HANDOVER,

	WTI_ATOM_LATENCY_MAX
};

struct wti_atom_offload_latency_entry {
	struct list_head list;
	enum wti_atom_latency_type type;
	uint16_t value;
};

struct wti_atom_offload_latency_data {
	struct mutex lock;
	struct list_head latency_list;
};

#endif // _WEARABLE_TOUCH_ATOM_H_
