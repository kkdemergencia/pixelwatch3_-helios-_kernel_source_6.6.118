// SPDX-License-Identifier: GPL-2.0
/*
 * Wear Touch MCU Interface of settings synchronization for Pixel wearable devices.
 *
 * Copyright 2024 Google LLC.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "wearable_touch_settings_synchronizer.h"
#include "wearable_touch_mcu.h"

// Structure to maintain all the settings including AP and MCU settings
struct wearable_touch_settings_synchronizer {
	struct mutex lock;
	struct wearable_touch_ap_settings ap_settings;
};

static struct wearable_touch_settings_synchronizer syncer = {
	.lock = __MUTEX_INITIALIZER(syncer.lock),
};

// Helper function to sync the information to MCU with holding the lock in synchronizer
static void wti_synchronizer_on_sync_locked(void)
{
	wti_mcu_send_settings(&syncer.ap_settings);
}

// Helper function to sync the information to MCU
void wti_synchronizer_on_sync(void)
{
	mutex_lock(&syncer.lock);
	wti_synchronizer_on_sync_locked();
	mutex_unlock(&syncer.lock);
}
EXPORT_SYMBOL(wti_synchronizer_on_sync);

/* Helper function to set up the touch lock settings and
 * sync this information to MCU at the same time.
 */
void wti_synchronizer_set_touch_lock(bool enable)
{
	mutex_lock(&syncer.lock);
	syncer.ap_settings.TouchLockEnabled = (uint8_t)enable;
	wti_synchronizer_on_sync_locked();
	mutex_unlock(&syncer.lock);
}
EXPORT_SYMBOL(wti_synchronizer_set_touch_lock);
