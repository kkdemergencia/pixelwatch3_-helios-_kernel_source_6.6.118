/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Google LLC */

#ifndef __GOOGLE_KAMINO_H__
#define __GOOGLE_KAMINO_H__

#include <linux/alarmtimer.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/power_supply.h>
#include <linux/timekeeping.h>
#include <linux/workqueue.h>
#include <misc/gvotable.h>

#define KAMINO_NUM_EVENTS_MAX 20

#define KAMINO_STS1_SET BIT(0)
#define KAMINO_STS2_SET BIT(1)

enum kamino_status {
	KAMINO_FAULT_INVALID_CHG	= 0,
	KAMINO_FAULT_MOISTURE		= KAMINO_STS1_SET,
	KAMINO_CHG_FAST			= KAMINO_STS2_SET,
	KAMINO_CHG_SLOW			= (KAMINO_STS1_SET | KAMINO_STS2_SET),
};

/* A status that happened at a particular time*/
struct kamino_event {
	enum kamino_status status;
	ktime_t time;
	bool faked;

	/* Note: Only used for moisture events. */
	bool in_reauth;
};

/*
 * Keeps the list_head separate from the actual event, since we copy to/from
 * the list item.
 */
struct kamino_event_item {
	struct kamino_event event;
	struct list_head elem;
};

struct kamino_event_timeline {
	struct mutex lock;
	struct list_head head;
	size_t num;

	/*
	 * A "pool" that we get an item from until exhausted, at which point
	 * we start overwriting existing items in the timeline.
	 */
	struct kamino_event_item free_items[KAMINO_NUM_EVENTS_MAX];
};

/*
 * There are two types of timings used to trigger reauth: oneshot and periodic.
 * The oneshot timings run after one another upon plugin. The first oneshot
 * timing runs X seconds after plugin. The next oneshot timing runs Y seconds
 * after the first oneshot timing, and so on.
 * Once the oneshot timings have been exhausted, the periodic timing is used to
 * schedule reauth, periodically until unplug.
 * E.g. if the oneshot timings were [10, 30], then we would force a reauth 10
 * seconds after plugin and 30 seconds after that (40 sec after plugin).
*/

#define NUM_KAMINO_ONESHOT_TIMINGS_MIN 0
#define NUM_KAMINO_ONESHOT_TIMINGS_MAX 5

struct kamino_reauth_timing {
	u32 ms;
	struct list_head elem;
};

struct kamino_reauth_timings {
	struct list_head oneshot_list;
	struct kamino_reauth_timing periodic;
	struct kamino_reauth_timing *curr_oneshot;
};

struct kamino_periodic_reauth {
	struct mutex lock;
	struct alarm alarm;
	struct kthread_work work;
	struct kthread_worker *worker;
	struct dentry *debugfs_dir;
	struct kamino_reauth_timings timings;
	u16 power_disconnect_min_wait_ms;
	u16 power_disconnect_timeout_ms;
	u16 power_reconnect_min_wait_ms;
	u16 power_reconnect_timeout_ms;
	bool enabled;
	bool in_progress;

	/*
	 * We request an ICL of 0 mA during fault conditions, which should mean
	 * the power supply reports as "offline" since there is no power
	 * actively being supplied to the system.
	 * However, if we fake the power supply online property, we override
	 * that behaviour. This is not ideal if we're latching moisture, so
	 * let the moisture mitigation tell us if faking should occur using
	 * google_kamino_periodic_reauth_set_psy_faking_enabled().
	 */
	bool psy_faking_en;
};

enum kamino_moisture_detect_mitigation_state {
	KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE = 0,

	/*
	 * Latch the moisture status for a duration of time. During this time,
	 * any charging statuses are ignored, mitigating the "false negative"
	 * case. This case occurs when there is a small amount of moisture,
	 * and/or its conductivity is poor, so the moisture detection flips
	 * between yes & no (the latter of which causes power to turn back on).
	 *
	 * Latching the moisture status also helps kamino status consumers,
	 * since the hardware only keeps the status for 10ms before releasing
	 * the GPIOs causing the idle "slow charging" status.
	 *
	 * If the HW generates another moisture event while we are latching,
	 * we restart the latching window.
	 */
	KAMINO_MOISTURE_DETECT_MITIGATION_LATCH_MOISTURE,

	/*
	 * Mitigate false positives that can occur upon placement of the watch
	 * in the dock by starting a window in which:
	 * - A new moisture event during this window indicates true positive
	 *   moisture.
	 * - Expiry of the window indicates true positive moisture.
	 * - Power connected closes the window and moves the status to
	 *   TRUE_POSITIVE_IF_IN_REAUTH
	 */
	KAMINO_MOISTURE_DETECT_MITIGATION_FALSE_POSITIVE_FILTER,

	/* True moisture if the event is attributed to a reauth */
	KAMINO_MOISTURE_DETECT_MITIGATION_TRUE_POSITIVE_IF_IN_REAUTH,
};

struct kamino_early_moisture_unlatch {
	struct alarm alarm;
	struct work_struct alarm_expire_work;
	unsigned int timeout_ms;
	bool alarm_active;
	bool enabled;
};

struct kamino_moisture_detect_mitigation {
	struct mutex lock;
	struct alarm alarm;
	struct kamino_early_moisture_unlatch early_unlatch;
	enum kamino_moisture_detect_mitigation_state state;
	struct work_struct alarm_expire_work;
	unsigned int moisture_latch_ms;
	unsigned int false_positive_filter_ms;
	bool fake_psy_in_false_positive;
	struct dentry *debugfs_dir;
};

struct kamino_extcon {
	struct mutex lock;
	struct extcon_dev *dev;
	struct dentry *debugfs_dir;
	bool spoof;
	bool spoof_val;
};

struct kamino_event_faker {
	struct mutex lock;
	struct dentry *debugfs_dir;
	bool enabled;
};

struct kamino {
	struct device *dev;
	struct gpio_desc *gpio_heartbeat_en;
	struct gpio_descs *gpios_status;
	unsigned int *status_irqs;
	struct power_supply *psy;
	struct device_link *psy_link;
	struct workqueue_struct *q;
	struct work_struct status_change_work;
	struct work_struct psy_changed_work;
	struct notifier_block psy_changed_nb;
	struct kamino_periodic_reauth periodic_reauth;
	struct kamino_moisture_detect_mitigation moisture_detect_mitigation;
	struct kamino_event_timeline event_timeline;
	enum kamino_status active_status;
	struct kamino_extcon extcon;
	struct kamino_event_faker event_faker;
	struct gvotable_election *icl_votable;
	struct gvotable_election *fake_psy_present_online_votable;
	struct dentry *debugfs_dir;
	u16 fast_chg_ma;
	u16 slow_chg_ma;
	bool charge_on_invalid_charger;
	bool psy_online_as_present;
};

static inline ktime_t google_kamino_gettime(void)
{
	return ktime_get_boottime();
}

static inline bool google_kamino_is_chg_status(enum kamino_status status)
{
	return (status == KAMINO_CHG_SLOW) || (status == KAMINO_CHG_FAST);
}

const char *google_kamino_status_str(enum kamino_status status);
bool google_kamino_psy_prop_present(struct kamino *kamino);
int google_kamino_psy_prop_voltage(struct kamino *kamino, int *uv);

void google_kamino_enable_irqs(struct kamino *kamino);
void google_kamino_disable_irqs(struct kamino *kamino);

void google_kamino_on_new_event(struct kamino *kamino,
				struct kamino_event *event);
void google_kamino_on_new_status(struct kamino *kamino);

#endif /* __GOOGLE_KAMINO_H__ */
