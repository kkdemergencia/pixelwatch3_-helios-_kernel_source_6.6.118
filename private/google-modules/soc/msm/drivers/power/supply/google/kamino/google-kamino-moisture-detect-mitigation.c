/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Google LLC */

#include <linux/ktime.h>
#include <linux/of.h>
#include "google-kamino.h"
#include "google-kamino-moisture-detect-mitigation.h"
#include "google-kamino-periodic-reauth.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#define MITIGATION_VOTER "MITIGATION_VOTER"

/*
 * Regardless of whether false positive is set to also fake PSY connected,
 * ensure that in a false positive we do _not_ propagate a moisture status.
 */
#define FALSE_POSITIVE_OVERRIDE_STATUS KAMINO_CHG_FAST

static inline bool
is_moisture_latch_en(const struct kamino_moisture_detect_mitigation *mit)
{
	return mit->moisture_latch_ms != 0;
}

static inline bool
is_latching_moisture(const struct kamino_moisture_detect_mitigation *mit)
{
	return KAMINO_MOISTURE_DETECT_MITIGATION_LATCH_MOISTURE == mit->state;
}

static inline bool
is_mitigating(const struct kamino_moisture_detect_mitigation *mit)
{
	return mit->state != KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE;
}

static bool
is_false_positive_filter_en(const struct kamino_moisture_detect_mitigation *mit)
{
	return mit->false_positive_filter_ms != 0;
}

static inline bool
is_filtering_false_positive(const struct kamino_moisture_detect_mitigation *mit)
{
	return KAMINO_MOISTURE_DETECT_MITIGATION_FALSE_POSITIVE_FILTER == mit->state;
}

static inline bool
early_unlatch_timeout_valid(const struct kamino_moisture_detect_mitigation *mit,
			const unsigned int timeout_ms)
{
	return (timeout_ms != 0) && (timeout_ms < mit->moisture_latch_ms);
}

static void fake_psy_present_online(struct kamino *kamino, bool fake)
{
	int ret;
	ret = gvotable_cast_bool_vote(kamino->fake_psy_present_online_votable,
				MITIGATION_VOTER, fake);
	if (ret < 0)
		dev_err(kamino->dev,
			"Could not vote to %s psy: %d\n",
			(fake ? "fake" : "stop faking"),
			ret);
}

static void start_early_unlatch_alarm(struct kamino *kamino)
{
	struct kamino_early_moisture_unlatch *unlatch =
		&kamino->moisture_detect_mitigation.early_unlatch;

	if (!unlatch->enabled)
		return;

	if (unlatch->timeout_ms == 0) {
		unlatch->enabled = false;
		dev_dbg(kamino->dev, "No early unlatch timeout set.\n");
		return;
	}

	dev_info(kamino->dev, "Starting early unlatch alarm (%u ms)\n",
		unlatch->timeout_ms);
	unlatch->alarm_active = true;
	alarm_start_relative(&unlatch->alarm, ms_to_ktime(unlatch->timeout_ms));
}

static void cancel_early_unlatch_alarm(struct kamino *kamino)
{
	struct kamino_early_moisture_unlatch *unlatch =
		&kamino->moisture_detect_mitigation.early_unlatch;
	unlatch->alarm_active = false;
	dev_info(kamino->dev, "Cancelling early unlatch alarm (if active)\n");
	alarm_try_to_cancel(&unlatch->alarm);
	cancel_work(&unlatch->alarm_expire_work);
}

static const char *state_str(enum kamino_moisture_detect_mitigation_state state)
{
	switch (state) {
	case KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE:
		return "Inactive";
	case KAMINO_MOISTURE_DETECT_MITIGATION_LATCH_MOISTURE:
		return "Latch Moisture";
	case KAMINO_MOISTURE_DETECT_MITIGATION_FALSE_POSITIVE_FILTER:
		return "False Positive Filter";
	case KAMINO_MOISTURE_DETECT_MITIGATION_TRUE_POSITIVE_IF_IN_REAUTH:
		return "Only Reauth Moisture Is True Positive";
	}
}

static void change_state(struct kamino *kamino,
			enum kamino_moisture_detect_mitigation_state new_state)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	bool reauth_fakes_psy = true;

	(void)alarm_try_to_cancel(&mit->alarm);

	dev_info(kamino->dev, "Mitigation state change: %s -> %s\n",
		state_str(mit->state), state_str(new_state));

	switch (new_state) {
	case KAMINO_MOISTURE_DETECT_MITIGATION_FALSE_POSITIVE_FILTER:
		fake_psy_present_online(kamino,
					mit->fake_psy_in_false_positive);
		break;
	case KAMINO_MOISTURE_DETECT_MITIGATION_LATCH_MOISTURE:
		reauth_fakes_psy = false;
		fallthrough;
	default:
		fake_psy_present_online(kamino, false);
	}

	/*
	 * Fault statuses result in an ICL of 0, which makes the PSY report
	 * offline. We do not want to override this if we're latching the
	 * moisture fault status and reauths start occurring.
	 */
	google_kamino_periodic_reauth_set_psy_faking_enabled(kamino,
							reauth_fakes_psy);
	mit->state = new_state;
}

static void start_mitigation(struct kamino *kamino,
			enum kamino_moisture_detect_mitigation_state state,
			unsigned int schedule_ms)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	change_state(kamino, state);
	alarm_start_relative(&mit->alarm, ms_to_ktime(schedule_ms));

	dev_info(kamino->dev, "Started %s alarm for %u ms\n",
		state_str(state), schedule_ms);
}

static int try_latch_moisture(struct kamino *kamino)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	if (!is_moisture_latch_en(mit))
		return -ENOTSUPP;

	start_mitigation(kamino,
			KAMINO_MOISTURE_DETECT_MITIGATION_LATCH_MOISTURE,
			mit->moisture_latch_ms);
	return 0;
}

static int try_filter_false_positive(struct kamino *kamino)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	if (!is_false_positive_filter_en(mit))
		return -ENOTSUPP;

	start_mitigation(kamino,
			KAMINO_MOISTURE_DETECT_MITIGATION_FALSE_POSITIVE_FILTER,
			mit->false_positive_filter_ms);
	return 0;
}

static void alarm_expire_work(struct work_struct *work)
{
	struct kamino_moisture_detect_mitigation
		*mit = container_of(
				work,
				struct kamino_moisture_detect_mitigation,
				alarm_expire_work);
	struct kamino *kamino
		= container_of(mit, struct kamino, moisture_detect_mitigation);

	mutex_lock(&mit->lock);

	cancel_early_unlatch_alarm(kamino);
	change_state(kamino, KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE);

	mutex_unlock(&mit->lock);
	queue_work(kamino->q, &kamino->status_change_work);
}

static void early_unlatch_alarm_expire_work(struct work_struct *work)
{
	struct kamino_early_moisture_unlatch *unlatch =
		container_of(
			work,
			struct kamino_early_moisture_unlatch,
			alarm_expire_work
		);
	struct kamino_moisture_detect_mitigation
		*mit = container_of(
				unlatch,
				struct kamino_moisture_detect_mitigation,
				early_unlatch);
	struct kamino *kamino
		= container_of(mit, struct kamino, moisture_detect_mitigation);

	mutex_lock(&mit->lock);

	/*
	 * If the alarm is inactive, it was cancelled either by
	 * google_kamino_moisture_detect_mitigation_on_new_status(), or by
	 * alarm_expire_work().
	 * In both cases we do not want to cause new state transitions, so
	 * we bail.
	 */
	if (!unlatch->alarm_active)
		goto out;

	unlatch->alarm_active = false;

	if (is_latching_moisture(mit)) {
		bool psy_present = google_kamino_psy_prop_present(kamino);
		alarm_try_to_cancel(&mit->alarm);

		/*
		 * Shortcut to reauth moisture being true positive, so that if
		 * this function ends up running before
		 * google_kamino_moisture_detect_mitigation_on_new_status() with
		 * a moisture status, we go back to latching moisture instead of
		 * filtering it out as a false positive.
		 */
		if (psy_present)
			change_state(
				kamino,
				KAMINO_MOISTURE_DETECT_MITIGATION_TRUE_POSITIVE_IF_IN_REAUTH
			);
		else
			change_state(kamino, KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE);

		queue_work(kamino->q, &kamino->status_change_work);
	}

out:
	mutex_unlock(&mit->lock);
}

static enum alarmtimer_restart
mitigation_alarm_expired(struct alarm *alarm, ktime_t time)
{
	struct kamino_moisture_detect_mitigation
		*mit = container_of(
				alarm,
				struct kamino_moisture_detect_mitigation,
				alarm);
	struct kamino *kamino
		= container_of(mit, struct kamino, moisture_detect_mitigation);

	dev_dbg(kamino->dev, "Mitigation alarm expired\n");

	queue_work(kamino->q, &mit->alarm_expire_work);
	return ALARMTIMER_NORESTART;
}

static enum alarmtimer_restart
early_unlatch_alarm_expired(struct alarm *alarm, ktime_t time)
{
	struct kamino_early_moisture_unlatch *early_unlatch =
		container_of(alarm,
			struct kamino_early_moisture_unlatch,
			alarm);
	struct kamino_moisture_detect_mitigation
		*mit = container_of(
				early_unlatch,
				struct kamino_moisture_detect_mitigation,
				early_unlatch);
	struct kamino *kamino
		= container_of(mit, struct kamino, moisture_detect_mitigation);

	dev_info(kamino->dev, "Early unlatch alarm expired\n");

	queue_work(kamino->q, &early_unlatch->alarm_expire_work);
	return ALARMTIMER_NORESTART;
}

void
google_kamino_moisture_detect_mitigation_on_new_status(struct kamino *kamino,
						const enum kamino_status status,
						const bool in_reauth)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	int ret;

	mutex_lock(&mit->lock);

	if (status != KAMINO_FAULT_MOISTURE)
		goto out;

	/* New moisture events cancel the early unlatch alarm */
	cancel_early_unlatch_alarm(kamino);

	switch (mit->state) {
	case KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE:
		ret = try_filter_false_positive(kamino);
		if (0 == ret)
			break;
		WARN_ON(ret != -ENOTSUPP);
		fallthrough;
	/*
	 * As noted in google-kamino-core.c#populate_event(), a moisture event
	 * may get attributed to the reauth since the two run independently.
	 * However, the chance of attributing false positive to reauth should
	 * be low.
	 */
	case KAMINO_MOISTURE_DETECT_MITIGATION_TRUE_POSITIVE_IF_IN_REAUTH:
		if (!in_reauth)
			break;
		fallthrough;
	default:
		ret = try_latch_moisture(kamino);
		WARN_ON((ret != 0) && (ret != -ENOTSUPP));
		break;
	}

out:
	mutex_unlock(&mit->lock);
}

int
google_kamino_moisture_detect_mitigation_override(struct kamino *kamino,
						struct kamino_event *override_out)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	int ret = 0;
	const ktime_t now = google_kamino_gettime();

	mutex_lock(&mit->lock);

	if (!is_moisture_latch_en(mit) && !is_false_positive_filter_en(mit)) {
		ret = -ENOTSUPP;
		goto out;
	}

	switch(mit->state) {
	case KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE:
	case KAMINO_MOISTURE_DETECT_MITIGATION_TRUE_POSITIVE_IF_IN_REAUTH:
		ret = -ENODATA;
		break;
	case KAMINO_MOISTURE_DETECT_MITIGATION_LATCH_MOISTURE:
		override_out->status = KAMINO_FAULT_MOISTURE;
		break;
	case KAMINO_MOISTURE_DETECT_MITIGATION_FALSE_POSITIVE_FILTER:
		override_out->status = FALSE_POSITIVE_OVERRIDE_STATUS;
		break;
	}

	if (0 == ret)
		override_out->time = now;

out:
	mutex_unlock(&mit->lock);
	return ret;
}

void google_kamino_moisture_detect_mitigation_psy_changed(struct kamino *kamino)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	struct kamino_early_moisture_unlatch *unlatch = &mit->early_unlatch;
	bool psy_present;
	int ret;
	int hw_uv;
	bool hw_connected;

	mutex_lock(&mit->lock);

	psy_present = google_kamino_psy_prop_present(kamino);
	ret = google_kamino_psy_prop_voltage(kamino, &hw_uv);
	hw_connected = (0 == ret) && (hw_uv != 0);

	/* Check the real HW status since we could be faking being connected. */
	if (is_filtering_false_positive(mit) && hw_connected) {
		alarm_try_to_cancel(&mit->alarm);

		change_state(kamino,
		KAMINO_MOISTURE_DETECT_MITIGATION_TRUE_POSITIVE_IF_IN_REAUTH);

		queue_work(kamino->q, &kamino->status_change_work);
		goto out;
	}

	/*
	 * PSY is saying it's connected, but we're not doing anything special.
	 * Choose to report only moisture event in reauth as true positives.
	 */
	if (!is_mitigating(mit) && psy_present) {
		change_state(kamino,
		KAMINO_MOISTURE_DETECT_MITIGATION_TRUE_POSITIVE_IF_IN_REAUTH);
		goto out;
	}

	/*
	 * However, PSY saying it's not connected means we can reset our state
	 * as long as we're not latched.
	 */
	if (is_mitigating(mit) && !is_latching_moisture(mit) && !psy_present) {
		change_state(kamino,
			KAMINO_MOISTURE_DETECT_MITIGATION_INACTIVE);
	}

	/*
	 * We can start an alarm to unlatch the moisture state early if we're
	 * in the moisture state but the PSY says it's connected. Alarm will
	 * get cancelled if a new moisture event comes in.
	 */
	if (unlatch->enabled && !unlatch->alarm_active
		&& is_latching_moisture(mit) && psy_present) {
		start_early_unlatch_alarm(kamino);
	}
out:
	mutex_unlock(&mit->lock);
}

#ifdef CONFIG_DEBUG_FS
static int latch_ms_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	mutex_lock(&mit->lock);
	*val = mit->moisture_latch_ms;
	mutex_unlock(&mit->lock);
	return 0;
}

static int latch_ms_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	int ret = 0;

	if (val >= UINT_MAX)
		return -EINVAL;

	mutex_lock(&mit->lock);

	mit->moisture_latch_ms = val;

	if (!is_latching_moisture(mit))
		goto out;

	if (mit->moisture_latch_ms) {
		dev_warn(kamino->dev,
			"Restarting latch due to updated timeout\n");
		ret = try_latch_moisture(kamino);
	} else {
		ret = alarm_try_to_cancel(&mit->alarm);
		if (ret > -1) {
			queue_work(kamino->q, &mit->alarm_expire_work);
			ret = 0;
		}
	}

out:
	mutex_unlock(&mit->lock);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(latch_ms_fops, latch_ms_get, latch_ms_set, "%llu\n");

static int false_positive_filter_ms_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	mutex_lock(&mit->lock);
	*val = mit->false_positive_filter_ms;
	mutex_unlock(&mit->lock);

	return 0;
}

static int false_positive_filter_ms_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	int ret = 0;

	if (val >= UINT_MAX)
		return -EINVAL;

	mutex_lock(&mit->lock);

	mit->false_positive_filter_ms = val;

	if (!is_filtering_false_positive(mit))
		goto out;

	alarm_cancel(&mit->alarm);
	if (mit->false_positive_filter_ms) {
		dev_warn(kamino->dev, "Restarting false positive window\n");
		ret = try_filter_false_positive(kamino);
	} else {
		ret = alarm_try_to_cancel(&mit->alarm);
		if (ret > -1) {
			queue_work(kamino->q, &mit->alarm_expire_work);
			ret = 0;
		}
	}

out:
	mutex_unlock(&mit->lock);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(false_positive_filter_ms_fops,
			false_positive_filter_ms_get,
			false_positive_filter_ms_set, "%llu\n");

static int fake_psy_in_false_positive_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	mutex_lock(&mit->lock);
	*val = mit->fake_psy_in_false_positive;
	mutex_unlock(&mit->lock);

	return 0;
}

static int fake_psy_in_false_positive_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	mutex_lock(&mit->lock);
	mit->fake_psy_in_false_positive = (val != 0);
	fake_psy_present_online(kamino,
				is_filtering_false_positive(mit)
				&& mit->fake_psy_in_false_positive);
	mutex_unlock(&mit->lock);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fake_psy_in_false_positive_fops,
			fake_psy_in_false_positive_get,
			fake_psy_in_false_positive_set, "%llu\n");

static int early_moisture_unlatch_enabled_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	mutex_lock(&mit->lock);
	*val = mit->early_unlatch.enabled;
	mutex_unlock(&mit->lock);

	return 0;
}

static int early_moisture_unlatch_enabled_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	int ret = 0;

	mutex_lock(&mit->lock);
	if (!early_unlatch_timeout_valid(mit, mit->early_unlatch.timeout_ms)) {
		ret = -EINVAL;

		/* Ensure early unlatch is disabled */
		val = 0;
	}

	mit->early_unlatch.enabled = (val != 0);
	if (mit->early_unlatch.enabled)
		queue_work(kamino->q, &kamino->psy_changed_work);
	else
		cancel_early_unlatch_alarm(kamino);

	mutex_unlock(&mit->lock);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(early_moisture_unlatch_enabled_fops,
			early_moisture_unlatch_enabled_get,
			early_moisture_unlatch_enabled_set, "%llu\n");

static int early_moisture_unlatch_timeout_ms_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	mutex_lock(&mit->lock);
	*val = mit->early_unlatch.timeout_ms;
	mutex_unlock(&mit->lock);

	return 0;
}

static int early_moisture_unlatch_timeout_ms_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	int ret = 0;

	mutex_lock(&mit->lock);
	if (!early_unlatch_timeout_valid(mit, val)) {
		ret = -EINVAL;
		goto out;
	}

	mit->early_unlatch.timeout_ms = val;

out:
	mutex_unlock(&mit->lock);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(early_moisture_unlatch_timeout_ms_fops,
			early_moisture_unlatch_timeout_ms_get,
			early_moisture_unlatch_timeout_ms_set, "%llu\n");

void
google_kamino_moisture_detect_mitigation_setup_debugfs(struct kamino *kamino)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	if (!kamino->debugfs_dir)
		return;

	mit->debugfs_dir = debugfs_create_dir("moisture_detect_mitigation",
					kamino->debugfs_dir);
	if (IS_ERR_OR_NULL(mit->debugfs_dir)) {
		mit->debugfs_dir = NULL;
		return;
	}

	debugfs_create_file("latch_ms", 0644, mit->debugfs_dir,
			kamino, &latch_ms_fops);
	debugfs_create_file("false_positive_filter_ms", 0644,
			mit->debugfs_dir, kamino,
			&false_positive_filter_ms_fops);
	debugfs_create_file("fake_psy_in_false_positive", 0644,
			mit->debugfs_dir, kamino,
			&fake_psy_in_false_positive_fops);
	debugfs_create_file("early_moisture_unlatch_enabled", 0644,
			mit->debugfs_dir, kamino,
			&early_moisture_unlatch_enabled_fops);
	debugfs_create_file("early_moisture_unlatch_timeout_ms", 0644,
			mit->debugfs_dir, kamino,
			&early_moisture_unlatch_timeout_ms_fops);
}
#endif

static ssize_t moisture_mitigation_state_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	enum kamino_moisture_detect_mitigation_state state;

	mutex_lock(&mit->lock);
	state = mit->state;
	mutex_unlock(&mit->lock);

	return sysfs_emit(buf, "%s\n", state_str(state));
}
static const DEVICE_ATTR_RO(moisture_mitigation_state);

void google_kamino_moisture_detect_mitigation_setup_sysfs(struct kamino *kamino)
{
	int ret = device_create_file(kamino->dev,
				&dev_attr_moisture_mitigation_state);
	if (ret < 0)
		dev_err(kamino->dev, "Failed to create %s: %d\n",
			dev_attr_moisture_mitigation_state.attr.name, ret);
}

void
google_kamino_moisture_detect_mitigation_teardown_sysfs(struct kamino *kamino)
{
	device_remove_file(kamino->dev, &dev_attr_moisture_mitigation_state);
}

int google_kamino_moisture_detect_mitigation_init(struct kamino *kamino)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;
	int ret;

	mutex_init(&mit->lock);
	alarm_init(&mit->alarm, ALARM_BOOTTIME, mitigation_alarm_expired);
	INIT_WORK(&mit->alarm_expire_work, alarm_expire_work);
	ret = of_property_read_u32(kamino->dev->of_node,
				"google,moisture-sw-latch-ms",
				&mit->moisture_latch_ms);
	if (ret != 0)
		mit->moisture_latch_ms = 0;

	ret = of_property_read_u32(kamino->dev->of_node,
				"google,moisture-false-positive-filter-ms",
				&mit->false_positive_filter_ms);
	if (ret != 0)
		mit->false_positive_filter_ms = 0;

	mit->fake_psy_in_false_positive
		= of_property_read_bool(kamino->dev->of_node,
			"google,moisture-false-positive-fakes-psy-connected");

	ret = of_property_read_u32(kamino->dev->of_node,
				"google,early-moisture-unlatch-timeout-ms",
				&mit->early_unlatch.timeout_ms);
	if (ret != 0)
		mit->early_unlatch.timeout_ms = 0;

	mit->early_unlatch.enabled =
		early_unlatch_timeout_valid(mit, mit->early_unlatch.timeout_ms);

	alarm_init(&mit->early_unlatch.alarm, ALARM_BOOTTIME,
		early_unlatch_alarm_expired);
	INIT_WORK(&mit->early_unlatch.alarm_expire_work,
		early_unlatch_alarm_expire_work);

	return 0;
}

void google_kamino_moisture_detect_mitigation_deinit(struct kamino *kamino)
{
	struct kamino_moisture_detect_mitigation
		*mit = &kamino->moisture_detect_mitigation;

	alarm_cancel(&mit->early_unlatch.alarm);
	cancel_work_sync(&mit->early_unlatch.alarm_expire_work);
	alarm_cancel(&mit->alarm);
	cancel_work_sync(&mit->alarm_expire_work);
	mutex_destroy(&mit->lock);
}
