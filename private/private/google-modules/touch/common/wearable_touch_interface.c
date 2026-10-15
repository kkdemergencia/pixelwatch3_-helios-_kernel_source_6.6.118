/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wear Touch Interface for Pixel wearable devices.
 *
 * Copyright 2024 Google LLC.
 */

#define pr_fmt(fmt) "touch: " fmt

#include <linux/circ_buf.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/rtc.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/fs.h>
#include "linux/types.h"
#include <linux/version.h>

#include "wearable_touch_data_capturer.h"
#include "wearable_touch_interface.h"
#include "wearable_touch_log.h"

// Wearable Touch Interface(WTI)
#define WTI_NAME "wti"

struct wearable_touch_handle {
	struct class *klass;
	struct mutex lock;
	struct kref refcount;
};

static struct wearable_touch_handle wt_handle = {
	.klass = NULL,
	.lock = __MUTEX_INITIALIZER(wt_handle.lock),
};

// Maximum number of touch events to store in the buffer
#define BUFFER_SIZE 64

static struct mutex buf_lock;

struct log_circ_buffer {
	struct circ_buf circ;
	struct log_event buffer[BUFFER_SIZE];
};

static struct log_circ_buffer log_buf;
static struct timespec64 boottime;

extern int wti_sysfs_init(struct wearable_touch_interface *wti);
extern void wti_sysfs_deinit(struct wearable_touch_interface *wti);

// Short gesture names are used to save output buffer space.
const char *log_gesture_type_names[GESTURE_TYPE_MAX] = {
	"Tap",		// tap to wake
	"Up",		// slide up to wake
	"Down",		// slide down to wake
	"Left",		// slide left to wake
	"Right",	// slide right to wake
	"Palm"		// plam on
};

static void init_log_circ_buffer(void)
{
	mutex_lock(&buf_lock);
	log_buf.circ.buf = (char *)log_buf.buffer;
	log_buf.circ.head = 0;
	log_buf.circ.tail = 0;
	memset(log_buf.buffer, 0, sizeof(log_buf.buffer));
	mutex_unlock(&buf_lock);
}

static void advance_pointers_locked(void)
{
	if (!CIRC_SPACE(log_buf.circ.head, log_buf.circ.tail, BUFFER_SIZE)) {
		log_buf.circ.tail = (log_buf.circ.tail + 1) % BUFFER_SIZE;
	}
	log_buf.circ.head = (log_buf.circ.head + 1) % BUFFER_SIZE;
	memset(&log_buf.buffer[log_buf.circ.head], 0, sizeof(struct log_event));
}

static void print_time(char *header, struct timespec64 *ts)
{
        struct rtc_time tm;

	rtc_time64_to_tm(ts->tv_sec, &tm);
	WTI_LOGI("%s: %02d:%02d:%02d.%03ld\n", header,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts->tv_nsec / 1000000);
}

/* Calculate the boot time in order to convert a monotonic time to
 * the calendar time.
 * This function does not take into consideration of the timezone and
 * daylight saving time because they should be managed in the user space.
 */
static void calc_boottime(void) {
	struct timespec64 ts_real, ts_mono;

	ts_real = ktime_to_timespec64(ktime_get_real());
	ts_mono = ktime_to_timespec64(ktime_get());
	boottime = timespec64_sub(ts_real, ts_mono);
	print_time("boot time (UTC)", &boottime);
}

static void update_timestamp_for_head_locked(ktime_t timestamp)
{
	struct log_event *evt = &log_buf.buffer[log_buf.circ.head];
	struct timespec64 ts = ktime_to_timespec64(timestamp);

	/* The boot time cannot be calculated in wear_touch_init()
	 * since the CLOCK_REALTIME may not be ready yet.
	 */
	if (boottime.tv_sec == 0)
		calc_boottime();

	evt->ts_end = timespec64_add(ts, boottime);
	if (evt->count++ == 0)
		evt->ts_bgn = evt->ts_end;
}

static inline bool is_motion_pending(void) {
	return (log_buf.buffer[log_buf.circ.head].type == MOTION &&
		log_buf.buffer[log_buf.circ.head].count > 0);
}

/* Add a gesture event to the circular buffer */
void wti_log_add_gesture(enum log_gesture_type gesture, ktime_t timestamp)
{
	struct log_event *evt;

	mutex_lock(&buf_lock);
	if (is_motion_pending())
		advance_pointers_locked();

	update_timestamp_for_head_locked(timestamp);
	evt = &log_buf.buffer[log_buf.circ.head];
	evt->type = GESTURE;
	evt->data.gesture = gesture;
	advance_pointers_locked();
	mutex_unlock(&buf_lock);
}
EXPORT_SYMBOL_GPL(wti_log_add_gesture);

/*
 * Add a motion event to the circular buffer.
 * Note that all motion events that occur between fingers down and up are
 * integrated into a single entry in the circular buffer to save space.
 * Support one finger motion logging for now.
 */
void wti_log_add_motion(uint32_t x, uint32_t y, uint8_t fingers, ktime_t timestamp)
{
	struct log_event *evt;

	mutex_lock(&buf_lock);
	update_timestamp_for_head_locked(timestamp);
	evt = &log_buf.buffer[log_buf.circ.head];
	evt->type = MOTION;
	// Note: The following delta calculations are designed for single-finger motion events.
	// They will need to be adjusted for two-finger motion events.
	if (evt->count > 1) {
		int32_t dx = x - evt->data.motion.last_x;
		int32_t dy = y - evt->data.motion.last_y;
		evt->data.motion.sum_dx += dx;
		evt->data.motion.sum_dy += dy;
		evt->data.motion.sum_abs_dx += abs(dx);
		evt->data.motion.sum_abs_dy += abs(dy);
	}
	evt->data.motion.last_x = x;
	evt->data.motion.last_y = y;
	++evt->finger_count[fingers - 1];
	mutex_unlock(&buf_lock);
}
EXPORT_SYMBOL_GPL(wti_log_add_motion);

/* A motion event entry is closed.
 * This is typically triggered when a finger up event is encountered.
 * The next motion or gesture event will be inserted into the next entry.
 */
void wti_log_close_motion(void)
{
	mutex_lock(&buf_lock);
	if (is_motion_pending())
		advance_pointers_locked();
	mutex_unlock(&buf_lock);
}
EXPORT_SYMBOL_GPL(wti_log_close_motion);

/* Reset the circular buffer. */
void wti_log_reset(void) {
	init_log_circ_buffer();
	WTI_LOGI("log reset!\n");
}
EXPORT_SYMBOL_GPL(wti_log_reset);

static const char *get_gesture_name(enum log_gesture_type gesture)
{
	if (gesture >= 0 && gesture < ARRAY_SIZE(log_gesture_type_names))
		return log_gesture_type_names[gesture];
	else
		return "unknown";
}

static ssize_t print_event_time(char *buf, uint32_t max_size, size_t used,
				struct log_event *evt)
{
	struct rtc_time tm;

	rtc_time64_to_tm(evt->ts_bgn.tv_sec, &tm);
	used += scnprintf(buf + used, max_size - used, "%02u:%02u:%02u.%03ld",
			  tm.tm_hour, tm.tm_min, tm.tm_sec,
			  evt->ts_bgn.tv_nsec / 1000000);

	return used;
}

static ssize_t print_gesture_event(char *buf, uint32_t max_size, size_t used,
				   struct log_event *evt)
{
	const char *gesture_name;

	used = print_event_time(buf, max_size, used, evt);

	gesture_name = get_gesture_name(evt->data.gesture);
	used += scnprintf(buf + used, max_size - used, " %s\n",
			   gesture_name);

	return used;
}

static ssize_t print_motion_events(char *buf, uint32_t max_size, size_t used,
				   struct log_event *evt)
{
	struct timespec64 diff;
	long diff_ms;

	used = print_event_time(buf, max_size, used, evt);

	/* Print the duration of the events and the MOTION name. */
	diff = timespec64_sub(evt->ts_end, evt->ts_bgn);
	diff_ms = diff.tv_sec * 1000 + diff.tv_nsec / 1000000;
	used += scnprintf(buf + used, max_size - used, " (%ld ms) ", diff_ms);

	/* Print the number of events for 1-finger and/or 2-finger events.
	 * finger_count[0]: the number of 1-finger motion events
	 * finger_count[1]: the number of 2-finger motion events
	 */
	if (evt->finger_count[1] == 0) {
		// No 2-finger events. Just print the 1-finger events.
		// Most motion events are 1-finger events.
		used += scnprintf(buf + used, max_size - used,
				   "%u mEvt\n", evt->finger_count[0]);
	} else if (evt->finger_count[0] == 0) {
		// No 1-finger events. Just print the 2-finger events.
		used += scnprintf(buf + used, max_size - used,
				   "2f:%u mEvt\n", evt->finger_count[1]);
	} else {
		// Print both 1-finger and 2-finger events.
		used += scnprintf(buf + used, max_size - used, "1f:%u 2f:%u mEvt\n",
				   evt->finger_count[0], evt->finger_count[1]);
	}

	return used;
}

ssize_t wti_dump_touchstats(char *buf, uint32_t max_size)
{
	int i;
	size_t used = 0;
	struct log_event *evt;

	mutex_lock(&buf_lock);
	used += scnprintf(buf + used, max_size - used, "Dump touchstats (UTC):\n");
	i = log_buf.circ.tail;
	while (i != log_buf.circ.head && max_size - used > 0) {
		evt = &log_buf.buffer[i];
		if (evt->type == GESTURE) {
			used = print_gesture_event(buf, max_size, used, evt);
		} else if (evt->type == MOTION) {
			used = print_motion_events(buf, max_size, used, evt);
		}
		i = (i + 1) % BUFFER_SIZE;
	}
	mutex_unlock(&buf_lock);

	return used;
}
EXPORT_SYMBOL_GPL(wti_dump_touchstats);

int wti_touchstats_init(void) {
	wti_log_reset();
	WTI_LOGI("touchstats init\n");
	return 0;
}
EXPORT_SYMBOL_GPL(wti_touchstats_init);

void wti_touchstats_deinit(void) {
	WTI_LOGI("touchstats deinit\n");
}
EXPORT_SYMBOL_GPL(wti_touchstats_deinit);

void wear_common_apis_init(struct wearable_touch_interface *wti, struct wti_common_apis *apis)
{
	if (apis && apis->get_fw_version)
		wti->common_apis.get_fw_version = apis->get_fw_version;
	if (apis && apis->get_display_resolution)
		wti->common_apis.get_display_resolution = apis->get_display_resolution;
	if (apis && apis->get_hardware_identifier)
		wti->common_apis.get_hardware_identifier = apis->get_hardware_identifier;
	if (apis && apis->get_grid_spec)
		wti->common_apis.get_grid_spec = apis->get_grid_spec;
}

static void wti_init_capturer(struct wearable_touch_interface *wti)
{
	mutex_init(&wti->capturer.lock);
	wti->capturer.wti_dev = wti->dev;
}

static void wti_class_destroy(struct kref *kref)
{
	class_destroy(wt_handle.klass);
	wt_handle.klass = NULL;
}

/* For now, we only support one module to create wear touch interface.
 * Return NULL: if there is a second module trying to create wear touch interface.
 */
struct wearable_touch_interface *wti_probe(
		void *private_data,
		struct device *dev,
		struct input_dev *input_dev,
		struct wti_common_apis *apis)
{
	int ret = 0;
	struct wearable_touch_interface *wti;
	char *name;

	if (!dev || !input_dev) {
		WTI_LOGE("%s: error: invalid dev/input_dev!\n", __func__);
		return NULL;
	}
	mutex_lock(&wt_handle.lock);
	wti = devm_kzalloc(dev, sizeof(struct wearable_touch_interface), GFP_KERNEL);
	if (!wti) {
		WTI_LOGE("%s: allocate wearable_touch_interface failed\n", __func__);
		goto exit_mem_allocate_failed;
	}
	wti->vendor_private_data = private_data;
	wti->vendor_dev = dev;
	wti->vendor_input_dev = input_dev;
	if (!wt_handle.klass) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
		wt_handle.klass = class_create(THIS_MODULE, "wear_touch_interface");
#else
		wt_handle.klass = class_create("wear_touch_interface");
#endif
		if (!wt_handle.klass) {
			WTI_LOGE("%s: class_create() failed, ret=%d!\n", __func__, ret);
			goto exit_class_create_failed;
		}
		kref_init(&wt_handle.refcount);
	} else {
		// Exit because wear touch interface was created by the other module.
		WTI_LOGE("%s: Wearable touch interface was created before\n", __func__);
		goto exit_wear_touch_interface_created;
	}

	name = kasprintf(GFP_KERNEL, "wti");
	WTI_LOGI("%s: Allocated name %s\n", __func__, name);

	if (!name) {
		WTI_LOGE("%s: allocate name for wti failed\n", __func__);
		goto exit_name_mem_allocate_failed;
	}

	if (alloc_chrdev_region(&wti->dev_id, 0, 1, name)) {
		WTI_LOGE("%s: allocate device id failed\n", __func__);
		goto exit_device_id_allocate_failed;
	}

	wti->dev = device_create(wt_handle.klass, NULL, wti->dev_id, wti, name);
	if (!wti->dev) {
		WTI_LOGE("%s: device_create() failed, ret=%d!\n", __func__, ret);
		goto exit_device_create_failed;
	}

	ret =  wti_sysfs_init(wti);
	if (ret) {
		WTI_LOGE("%s: sysfs init failed, ret=%d!\n", __func__, ret);
		goto exit_sysfs_init_failed;
	}

	wear_common_apis_init(wti, apis);

	wti_init_capturer(wti);

	mutex_init(&wti->atom_data.offload_latency_data.lock);
	INIT_LIST_HEAD(&wti->atom_data.offload_latency_data.latency_list);

	mutex_init(&buf_lock);
	init_log_circ_buffer();
	memset(&boottime, 0, sizeof(boottime));
	WTI_LOGI("wear touch interface driver loaded\n");

	kfree(name);

	mutex_unlock(&wt_handle.lock);
	return wti;

exit_sysfs_init_failed:
	device_destroy(wt_handle.klass, wti->dev_id);
exit_device_create_failed:
	unregister_chrdev_region(wti->dev_id, 1);
exit_device_id_allocate_failed:
	kfree(name);
exit_name_mem_allocate_failed:
	kref_put(&wt_handle.refcount, wti_class_destroy);
exit_class_create_failed:
exit_wear_touch_interface_created:
	devm_kfree(dev, wti);
exit_mem_allocate_failed:
	mutex_unlock(&wt_handle.lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(wti_probe);

int wti_remove(struct wearable_touch_interface *wti)
{
	if (!wti)
		return -ENODEV;
	mutex_lock(&wt_handle.lock);
	if (wti->dev) {
		wti_sysfs_deinit(wti);
		device_destroy(wt_handle.klass, wti->dev_id);
		wti->dev = NULL;
		unregister_chrdev_region(wti->dev_id, 1);
		kref_put(&wt_handle.refcount, wti_class_destroy);
	}
	mutex_unlock(&wt_handle.lock);

	mutex_destroy(&wti->atom_data.offload_latency_data.lock);

	devm_kfree(wti->vendor_dev, wti);
	mutex_destroy(&buf_lock);
	WTI_LOGI("wear touch interface driver unloaded\n");

	return 0;
}
EXPORT_SYMBOL_GPL(wti_remove);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Google Pixel Wear Touch Team");
MODULE_DESCRIPTION("Wear Touch Interface");
