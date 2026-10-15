
/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Google LLC */

#include <linux/alarmtimer.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <misc/gvotable.h>
#include "google-kamino.h"
#include "google-kamino-periodic-reauth.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_buf.h>
#endif

#define PERIODIC_REAUTH_VOTER "PERIODIC_REAUTH_VOTER"

#define VOLTAGE_COMPARES	\
	X(EQUAL) 		\
	X(ABOVE)

#define X(cmp) VOLTAGE_##cmp,
enum voltage_compare {
	VOLTAGE_COMPARES
	NUM_COMPARES
};
#undef X

#define X(cmp) #cmp,
static const char *voltage_compare_str[NUM_COMPARES] = {
	VOLTAGE_COMPARES
};
#undef X

static inline bool is_hw_really_disconnected(struct kamino *kamino)
{
	int uv;
	int ret = google_kamino_psy_prop_voltage(kamino, &uv);

	return (ret != 0) || (uv == 0);
}

static void schedule_reauth(struct kamino *kamino)
{
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;
	struct kamino_reauth_timings *timings = &reauth->timings;
	bool disconnected =
		!google_kamino_periodic_reauth_in_progress(kamino)
		&& is_hw_really_disconnected(kamino);

	if ((disconnected || !reauth->enabled)) {
		if (timings->curr_oneshot) {
			dev_info(kamino->dev, "Cancelling reauth\n");
			alarm_try_to_cancel(&reauth->alarm);
			timings->curr_oneshot = NULL;
		}
		return;
	}

	if (!(reauth->alarm.state & ALARMTIMER_STATE_ENQUEUED)) {
		struct kamino_reauth_timing *timing;
		struct list_head *oneshot_list = &timings->oneshot_list;

		if (!timings->curr_oneshot && !list_empty(oneshot_list)) {
			timings->curr_oneshot =
				list_first_entry(oneshot_list,
						struct kamino_reauth_timing,
						elem);
			timing = timings->curr_oneshot;
		} else if (list_empty(oneshot_list) ||
			list_is_last(&timings->curr_oneshot->elem, oneshot_list)) {
			timing = &timings->periodic;
		} else {
			timings->curr_oneshot =
				list_next_entry(timings->curr_oneshot, elem);
			timing = timings->curr_oneshot;
		}

		dev_info(kamino->dev, "Scheduling reauth for %u ms\n",
			timing->ms);
		alarm_start_relative(&reauth->alarm, ms_to_ktime(timing->ms));
	}
}

void google_kamino_periodic_reauth_psy_changed(struct kamino *kamino)
{
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	if (!reauth->enabled)
		return;

	mutex_lock(&reauth->lock);
	schedule_reauth(kamino);
	mutex_unlock(&reauth->lock);
}

static void fake_psy_present_online(struct kamino *kamino, bool fake)
{
	int ret;
	ret = gvotable_cast_bool_vote(kamino->fake_psy_present_online_votable,
		PERIODIC_REAUTH_VOTER, fake);
	if (ret < 0)
		dev_err(kamino->dev,
			"Could not vote to %s psy: %d\n",
			(fake ? "fake" : "stop faking"),
			ret);
}

static void disable_current(struct kamino *kamino, bool disable) {
	int ret;
	ret = gvotable_cast_int_vote(kamino->icl_votable,
				PERIODIC_REAUTH_VOTER, 0, disable);
	if (ret < 0)
		dev_err(kamino->dev,
			"Could not vote to %s current: %d\n",
			(disable ? "disable" : "enable"),
			ret);
}

void google_kamino_periodic_reauth_set_psy_faking_enabled(struct kamino *kamino,
							bool faking_enabled)
{
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	mutex_lock(&reauth->lock);
	reauth->psy_faking_en = faking_enabled;
	mutex_unlock(&reauth->lock);
}

/*
 * Waits up until the timeout for the PSY voltage to reach the condition as
 * an optimization to get back on-power quicker.
 * Note: We are using the voltage since we ask the PSY to fake its
 *       present/online status. However, there is no guarantee that the PSY
 *       wouldn't also fake voltage. Thus, this optimization may not work in all
 *       cases, hence the fallback to the timeout.
 */
static void wait_for_any_voltage_timeout_status(struct kamino *kamino,
						enum voltage_compare compare,
						int thresh_uv,
						unsigned int min_wait_ms,
						unsigned int timeout_ms,
						enum kamino_status *status) {
	const unsigned int iter_wait_us = 10 /*ms*/ * 1000;
	const unsigned int min_wait_us = min_wait_ms * 1000;
	const unsigned int timeout_us = timeout_ms * 1000;
	const unsigned int time_delta_us = (timeout_us - min_wait_us);
	const unsigned int num_iterations = (time_delta_us / iter_wait_us);
	const unsigned int rem_us = (time_delta_us % iter_wait_us);
	int i;
	int ret;
	int uv;

	if (min_wait_us >= timeout_us) {
		dev_err(kamino->dev,
			"Min wait of %u us >= timeout of %u us\n",
			min_wait_us, timeout_us);
		dev_err(kamino->dev, "Using timeout duration as only wait\n");
		usleep_range(timeout_us, timeout_us);
		goto out;
	}

	usleep_range(min_wait_us, min_wait_us);

	for (i = 0; i < num_iterations; i++) {
		const enum kamino_status
			active = READ_ONCE(kamino->active_status);

		if (status && (*status == active)) {
			dev_dbg(kamino->dev, "status %s matched\n",
				google_kamino_status_str(*status));
			return;
		}

		ret = google_kamino_psy_prop_voltage(kamino, &uv);
		if (ret != 0)
			goto wait;

		switch (compare) {
		case VOLTAGE_EQUAL:
			if (uv == thresh_uv) {
				dev_dbg(kamino->dev, "%d == %d\n",
					uv, thresh_uv);
				return;
			}
			break;
		case VOLTAGE_ABOVE:
			if (uv > thresh_uv) {
				dev_dbg(kamino->dev, "%d > %d\n",
				uv, thresh_uv);
				return;
			}
			break;
		default:
			dev_err(kamino->dev, "Unhandled comparison type %s!\n",
				voltage_compare_str[compare]);
		}
wait:
		/*
		 * Note: Using usleep for higher precision than msleep.
		 * The sooner we exit this function, the sooner the device
		 * gets back to being supplied by the power source.
		 */
		usleep_range(iter_wait_us, iter_wait_us);
	}

	if (rem_us)
		usleep_range(rem_us, rem_us);

out:
	ret = google_kamino_psy_prop_voltage(kamino, &uv);
	dev_warn(kamino->dev,
		"%s %d uV %d ms timeout reached. Voltage now: %d uV (ret %d)\n",
		voltage_compare_str[compare], thresh_uv, timeout_ms, uv, ret);
}

static void reauth_work(struct kthread_work *work)
{
	struct kamino_periodic_reauth *reauth
		= container_of(work, struct kamino_periodic_reauth, work);
	struct kamino *kamino
		= container_of(reauth, struct kamino, periodic_reauth);
	enum kamino_status desired;

	if (!google_kamino_psy_prop_present(kamino))
		return;

	mutex_lock(&reauth->lock);

	if (!reauth->enabled)
		goto out;

	dev_warn(kamino->dev, "Forcing reauth!\n");

	/*
	 * If applicable, tell power supply to fake present/online since
	 * Kamino will briefly disconnect power.
	 */
	fake_psy_present_online(kamino, reauth->psy_faking_en);

	/* Disable current draw from USB */
	disable_current(kamino, true);

	/* Enforce that the above happens before we say we're in progress */
	smp_mb();

	/* Ensure in-progress status is seen by other CPUs */
	smp_store_mb(reauth->in_progress, true);

	/*
	 * Turn off heartbeat. Combined with suspending current draw, this makes
	 * Kamino go back to idle, disconnecting power while doing so.
	 */
	gpiod_set_value(kamino->gpio_heartbeat_en, false);

	/* It takes some time for Kamino to go idle & disconnect power */
	wait_for_any_voltage_timeout_status(kamino, VOLTAGE_EQUAL, 0,
					reauth->power_disconnect_min_wait_ms,
					reauth->power_disconnect_timeout_ms,
					NULL);

	/* Reset the heartbeat */
	gpiod_set_value(kamino->gpio_heartbeat_en, true);

	disable_current(kamino, false);

	/*
	 * Keep faking present status for the typical amount of time we
	 * expect to take Kamino to turn power back on.
	 */
	desired = KAMINO_FAULT_MOISTURE;
	wait_for_any_voltage_timeout_status(kamino, VOLTAGE_ABOVE, 5000000,
					reauth->power_reconnect_min_wait_ms,
					reauth->power_reconnect_timeout_ms,
					&desired);

	/* Enforce that the above happens before we start wrapping up */
	smp_mb();

	/*
	 * Reauth considered finished; reset in-progress status ensuring CPUs
	 * see this update and order against the final touches.
	 */
	smp_store_mb(reauth->in_progress, false);

	schedule_reauth(kamino);

	/* No faking needed anymore */
	fake_psy_present_online(kamino, false);

out:
	mutex_unlock(&reauth->lock);
}

static enum alarmtimer_restart
reauth_alarm_expired(struct alarm *alarm, ktime_t time)
{
	struct kamino_periodic_reauth *reauth
		= container_of(alarm, struct kamino_periodic_reauth, alarm);
	struct kamino *kamino
		= container_of(reauth, struct kamino, periodic_reauth);

	dev_dbg(kamino->dev, "Reauth alarm expired\n");
	kthread_queue_work(reauth->worker, &reauth->work);
	return ALARMTIMER_NORESTART;
}

#ifdef CONFIG_DEBUG_FS

static int enabled_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	*val = reauth->enabled;

	return 0;
}

static int enabled_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	bool enabled = (val != 0);
	if (enabled == reauth->enabled)
		return 0;

	if (enabled && !kamino->gpio_heartbeat_en) {
		return -ENOTSUPP;
	}

	mutex_lock(&reauth->lock);

	reauth->enabled = enabled;

	if (reauth->enabled) {
		schedule_reauth(kamino);
	} else {
		alarm_cancel(&reauth->alarm);
		kthread_cancel_work_sync(&reauth->work);
		reauth->timings.curr_oneshot = NULL;
	}

	mutex_unlock(&reauth->lock);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(enabled_fops, enabled_get, enabled_set, "%llu\n");

static int periodic_timing_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	*val = reauth->timings.periodic.ms;

	return 0;
}

static int periodic_timing_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	if (val > U32_MAX)
		return -EOVERFLOW;

	mutex_lock(&reauth->lock);

	reauth->timings.periodic.ms = val;

	if (reauth->enabled) {
		alarm_cancel(&reauth->alarm);
		kthread_cancel_work_sync(&reauth->work);
		reauth->timings.curr_oneshot = NULL;
		schedule_reauth(kamino);
	}

	mutex_unlock(&reauth->lock);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(periodic_timing_fops, periodic_timing_get,
			periodic_timing_set, "%llu\n");

/* Each timing entry can each be at most (6 digits + space + comma/null byte) */
#define KAMINO_DEBUGFS_ONESHOT_TIMING_ENTRY_SIZE 7

/* Add a byte for newline to be used when outputting to console */
#define KAMINO_DEBUGFS_ONESHOT_TIMINGS_SIZE \
	((NUM_KAMINO_ONESHOT_TIMINGS_MAX * KAMINO_DEBUGFS_ONESHOT_TIMING_ENTRY_SIZE) + 1)

static ssize_t oneshot_timings_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct kamino *kamino = (struct kamino *)file->private_data;
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;
	struct list_head *curr;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0))
	char mem[KAMINO_DEBUGFS_ONESHOT_TIMINGS_SIZE];
	struct seq_buf buf;

	seq_buf_init(&buf, mem, sizeof(mem));
#else
	DECLARE_SEQ_BUF(buf, KAMINO_DEBUGFS_ONESHOT_TIMINGS_SIZE);
#endif

	if (list_empty(&reauth->timings.oneshot_list)) {
		seq_buf_printf(&buf, "none\n");
		goto out;
	}

	list_for_each(curr, &reauth->timings.oneshot_list) {
		struct kamino_reauth_timing *timing =
			list_entry(curr, struct kamino_reauth_timing, elem);
		const char *sep =
			list_is_last(curr,
				&reauth->timings.oneshot_list) ? "" : ", ";
		seq_buf_printf(&buf, "%u%s", timing->ms, sep);
	}

	seq_buf_printf(&buf, "\n");

out:
	if (seq_buf_has_overflowed(&buf))
		return -1;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0))
	seq_buf_terminate(&buf);
	return simple_read_from_buffer(user_buf, count, ppos,
				mem, seq_buf_used(&buf));
#else
	return simple_read_from_buffer(user_buf, count, ppos,
				seq_buf_str(&buf), seq_buf_used(&buf));
#endif
}

static ssize_t oneshot_timings_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct kamino *kamino = (struct kamino *)file->private_data;
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;
	char mem[KAMINO_DEBUGFS_ONESHOT_TIMINGS_SIZE] = {0};
	int i;
	int num_timings = 0;
	struct list_head *curr;
	int ret;
	char **split;
	int split_count;
	u32 new_timings[NUM_KAMINO_ONESHOT_TIMINGS_MAX];

	list_for_each(curr, &reauth->timings.oneshot_list) {
		num_timings++;
	}

	ret = simple_write_to_buffer(mem, KAMINO_DEBUGFS_ONESHOT_TIMINGS_SIZE,
				ppos, user_buf, count);
	if (ret < 0)
		return ret;

	strreplace(mem, ',', ' ');
	split = argv_split(GFP_KERNEL, mem, &split_count);

	/*
	 * Developers can only specify the same number of oneshot timings as
	 * defined in the devicetree.
	 */
	if (split_count != num_timings) {
		ret = -EINVAL;
		goto out;
	}

	/* Make sure that all timings are numbers before proceeding */
	for (i = 0; i < num_timings; i++) {
		ret = kstrtou32(split[i], 0, &new_timings[i]);
		if (ret != 0)
			goto out;
	}

	mutex_lock(&reauth->lock);

	i = 0;
	list_for_each(curr, &reauth->timings.oneshot_list) {
		struct kamino_reauth_timing *timing =
			list_entry(curr, struct kamino_reauth_timing, elem);
		timing->ms = new_timings[i++];
	}

	if (reauth->enabled) {
		alarm_cancel(&reauth->alarm);
		kthread_cancel_work_sync(&reauth->work);
		reauth->timings.curr_oneshot = NULL;
		schedule_reauth(kamino);
	}
	ret = count;
	mutex_unlock(&reauth->lock);

out:
	argv_free(split);
	return ret;
}

static const struct file_operations oneshot_timings_fops = {
	.open	= simple_open,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	.llseek	= no_llseek,
#endif
	.read	= oneshot_timings_read,
	.write	= oneshot_timings_write,
};

void google_kamino_periodic_reauth_setup_debugfs(struct kamino *kamino)
{
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	if (!kamino->debugfs_dir)
		return;

	reauth->debugfs_dir = debugfs_create_dir("periodic_reauth",
						kamino->debugfs_dir);

	if (IS_ERR_OR_NULL(reauth->debugfs_dir)) {
		reauth->debugfs_dir = NULL;
		return;
	}

	debugfs_create_u16("power_disconnect_min_wait_ms", 0644,
			reauth->debugfs_dir,
			&reauth->power_disconnect_min_wait_ms);
	debugfs_create_u16("power_disconnect_timeout_ms", 0644,
			reauth->debugfs_dir,
			&reauth->power_disconnect_timeout_ms);
	debugfs_create_u16("power_reconnect_min_wait_ms", 0644,
			reauth->debugfs_dir,
			&reauth->power_reconnect_min_wait_ms);
	debugfs_create_u16("power_reconnect_timeout_ms", 0644,
			reauth->debugfs_dir,
			&reauth->power_reconnect_timeout_ms);
	debugfs_create_file("enabled", 0644, reauth->debugfs_dir,
			kamino, &enabled_fops);
	debugfs_create_file("oneshot_timings_ms", 0644, reauth->debugfs_dir,
			kamino, &oneshot_timings_fops);
	debugfs_create_file("periodic_timing_ms", 0644, reauth->debugfs_dir,
			kamino, &periodic_timing_fops);
}
#endif

static int init_oneshot_timings(struct kamino *kamino) {
	int i;
	int num;
	u32 dt_timings[NUM_KAMINO_ONESHOT_TIMINGS_MAX];
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;
	/* Actual timings storage; must be allocated for module's lifetime. */
	struct kamino_reauth_timing *timings;

	INIT_LIST_HEAD(&reauth->timings.oneshot_list);
	reauth->timings.curr_oneshot = NULL;
	num = of_property_read_variable_u32_array(kamino->dev->of_node,
					"google,reauth-oneshot-timings-ms",
					dt_timings,
					NUM_KAMINO_ONESHOT_TIMINGS_MIN,
					NUM_KAMINO_ONESHOT_TIMINGS_MAX);
	if (num < 0) {
		dev_warn(kamino->dev,
			"Oneshot timings DT read error: %d\n", num);
		return 0;
	}

	if (num == 0)
		return 0;

	timings = devm_kcalloc(kamino->dev, num, sizeof(*timings), GFP_KERNEL);
	if (!timings)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		timings[i].ms = dt_timings[i];
		list_add_tail(&timings[i].elem, &reauth->timings.oneshot_list);
	}

	return 0;
}

int google_kamino_periodic_reauth_init(struct kamino *kamino)
{
	int ret;
	struct kamino_periodic_reauth *reauth = &kamino->periodic_reauth;

	mutex_init(&reauth->lock);

	reauth->worker = kthread_create_worker(0, "%s",
					"kamino-periodic-reauth-worker");
	if (IS_ERR(reauth->worker)) {
		dev_err(kamino->dev,
			"Could not create periodic reauth worker: %ld\n",
			PTR_ERR(reauth->worker));
		return PTR_ERR(reauth->worker);
	}

	reauth->enabled =
		kamino->gpio_heartbeat_en &&
		of_property_read_bool(kamino->dev->of_node,
				"google,periodic-reauth-enable");

	ret = init_oneshot_timings(kamino);
	if (ret < 0) {
		kthread_destroy_worker(reauth->worker);
		return ret;
	}

	ret = of_property_read_u32(kamino->dev->of_node,
				"google,reauth-periodic-timing-ms",
				&reauth->timings.periodic.ms);
	if (ret < 0) {
		reauth->timings.periodic.ms = 180000;
	}

	/*
	 * The charger IC uses a 125ms window in which the current draw from the
	 * watch must be greater than 100mA. If this is not seen, it goes to
	 * idle and disconnects power. To ensure no power cutouts, the watch IC
	 * provides a 100mA heartbeat that runs every 100ms.
	 * Theoretically, if we were to turn off the heartbeat and disable
	 * current draw at the right time, there would be a 25ms delay until the
	 * watch sees 0V power. In practice however, there is a delay that is
	 * dependent on the PMIC circuitry, and reaching this ideal timing is
	 * difficuit given that we do not know exactly when the heartbeat runs.
	 * We expect the DT property to be set appropriately for this, so that
	 * we sleep longer when we can do so.
	 */
	ret = of_property_read_u16(kamino->dev->of_node,
				"google,reauth-power-disconnect-min-wait-ms",
				&reauth->power_disconnect_min_wait_ms);
	if (ret < 0) {
		reauth->power_disconnect_min_wait_ms = 25;
	}

	ret = of_property_read_u16(kamino->dev->of_node,
				"google,reauth-power-disconnect-timeout-ms",
				&reauth->power_disconnect_timeout_ms);
	if (ret < 0) {
		reauth->power_disconnect_timeout_ms = 200;
	}

	ret = of_property_read_u16(kamino->dev->of_node,
				"google,reauth-power-reconnect-min-wait-ms",
				&reauth->power_reconnect_min_wait_ms);
	if (ret < 0) {
		/* 10ms fault wait + 60ms power-up cycle */
		reauth->power_reconnect_min_wait_ms = 70;
	}

	ret = of_property_read_u16(kamino->dev->of_node,
				"google,reauth-power-reconnect-timeout-ms",
				&reauth->power_reconnect_timeout_ms);
	if (ret < 0) {
		reauth->power_reconnect_timeout_ms = 200;
	}

	kthread_init_work(&kamino->periodic_reauth.work, reauth_work);
	alarm_init(&kamino->periodic_reauth.alarm, ALARM_BOOTTIME,
		reauth_alarm_expired);

	return 0;
}

/* Note: google-kamino-core.c will remove all debugfs entries */
void google_kamino_periodic_reauth_deinit(struct kamino *kamino) {
	mutex_lock(&kamino->periodic_reauth.lock);
	kamino->periodic_reauth.enabled = false;
	mutex_unlock(&kamino->periodic_reauth.lock);
	alarm_cancel(&kamino->periodic_reauth.alarm);
	kthread_cancel_work_sync(&kamino->periodic_reauth.work);
	kthread_destroy_worker(kamino->periodic_reauth.worker);
	mutex_destroy(&kamino->periodic_reauth.lock);
}

void google_kamino_periodic_reauth_pm_suspend(struct kamino *kamino)
{
	kthread_flush_worker(kamino->periodic_reauth.worker);
}
