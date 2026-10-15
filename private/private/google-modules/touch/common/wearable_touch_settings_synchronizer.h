/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wear Touch MCU Interface of settings synchronization for Pixel wearable devices.
 *
 * Copyright 2025 Google LLC.
 */
#ifndef _WEARABLE_TOUCH_SETTINGS_SYNCHRONIZATION_H_
#define _WEARABLE_TOUCH_SETTINGS_SYNCHRONIZATION_H_

#include <linux/types.h>

// All the AP settings needed to be synced from AP to MCU
struct wearable_touch_ap_settings {
	uint8_t TouchLockEnabled;
} __packed;

// Helper function to sync the information to MCU
extern void wti_synchronizer_on_sync(void);

/* Helper function to set up the touch lock settings and
 * sync this information to MCU at the same time.
 */
extern void wti_synchronizer_set_touch_lock(bool enable);

#endif // _WEARABLE_TOUCH_SETTINGS_SYNCHRONIZATION_H_
