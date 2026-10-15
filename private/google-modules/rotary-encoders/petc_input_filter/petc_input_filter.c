/*
 * Copyright (C) 2023 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#include "petc_input_filter.h"
#include "mcu_display_exports.h"

#define WAKEUP_WAIT_MAX msecs_to_jiffies(500)
#define GATING_DURATION 800  // milliseconds
#define PETC_PREFIX "petc_"

struct petc_input_event {
	struct list_head node;
	ktime_t timestamp;
	unsigned int type;
	unsigned int code;
	int value;
};

struct petc_if_dev_data {
	struct list_head node;
	struct input_handle handle;
	struct work_struct register_work;
	struct work_struct immediate_work;
	struct delayed_work delayed_work;
	struct list_head event_list;
	spinlock_t event_list_slock;
	struct input_dev *clone_dev;
	struct petc_if_drv_data *drv_data;
	char *clone_name;
	bool clone_registered;
};

struct petc_if_clone_config {
	struct device_node *source_np;
};

struct petc_if_drv_data {
	struct platform_device *pdev;
	struct input_handler *handler;
	struct drm_panel *active_panel;
	struct work_struct remove_devices_work;
	struct list_head device_list;
	struct list_head removed_device_list;
	spinlock_t device_list_slock;
	void *notifier_cookie;
	struct input_dev *key_dev;
	struct petc_if_clone_config *clone_configs;
	int num_clone_configs;
	int ap_screen;
	ktime_t wheel_event_gate_end_time;
};

static bool petc_if_gating_wheel_events(struct petc_if_drv_data *drv_data,
					ktime_t timestamp)
{
	if (ktime_compare(timestamp, drv_data->wheel_event_gate_end_time) < 0) {
			pr_debug("[PETC_IF] Gating events\n");
			return true;
	}

	pr_debug("[PETC_IF] Not gating events\n");

	return false;
}

static bool petc_if_is_mcu_screen_on(int mcu_disp_state)
{
	pr_debug("[PETC_IF] MCU display state = %d\n", mcu_disp_state);
	return (mcu_disp_state == MCU_DISPLAY_ON ||
		mcu_disp_state == MCU_DISPLAY_HIGH_BRIGHTNESS);
}

static bool petc_if_is_mcu_screen_off(int mcu_disp_state)
{
	pr_debug("[PETC_IF] MCU display state = %d\n", mcu_disp_state);
	return (mcu_disp_state == MCU_DISPLAY_OFF ||
		mcu_disp_state == MCU_DISPLAY_NONE);
}

static bool petc_if_is_ap_screen_on(struct petc_if_drv_data *drv_data)
{
	pr_debug("[PETC_IF] AP display state = %d\n", drv_data->ap_screen);
	return drv_data->ap_screen == DRM_PANEL_EVENT_UNBLANK;
}

static bool petc_if_is_ap_screen_lp(struct petc_if_drv_data *drv_data)
{
	pr_debug("[PETC_IF] AP display state = %d\n", drv_data->ap_screen);
	return drv_data->ap_screen == DRM_PANEL_EVENT_BLANK_LP;
}

static bool petc_if_is_display_in_transition(struct petc_if_drv_data *drv_data,
						int mcu_disp_state)
{
	return petc_if_is_mcu_screen_off(mcu_disp_state) &&
		petc_if_is_ap_screen_lp(drv_data);
}

static bool petc_if_is_false_interactive(struct petc_if_dev_data *dev_data,
					 int mcu_disp_state)
{
	return !petc_if_is_ap_screen_on(dev_data->drv_data) &&
		petc_if_is_mcu_screen_on(mcu_disp_state);
}

static void petc_if_send_key(ktime_t timestamp,
			     struct petc_if_dev_data *dev_data,
			     uint32_t keycode)
{
	struct input_dev *key_dev = dev_data->drv_data->key_dev;

	input_set_timestamp(key_dev, timestamp);
	input_report_key(key_dev, keycode, 1);
	input_sync(key_dev);

	input_set_timestamp(key_dev, timestamp);
	input_report_key(key_dev, keycode, 0);
	input_sync(key_dev);
}

static void petc_if_add_device(struct petc_if_drv_data *drv_data,
			       struct petc_if_dev_data *dev_data)
{
	unsigned long flags;

	spin_lock_irqsave(&drv_data->device_list_slock, flags);
	list_add_tail(&dev_data->node, &drv_data->device_list);
	spin_unlock_irqrestore(&drv_data->device_list_slock, flags);
}

static void petc_if_queue_event(struct petc_if_dev_data *dev_data,
				ktime_t timestamp, unsigned int type,
				unsigned int code, int value)
{
	struct petc_input_event *event;
	unsigned long flags;

	event = kmalloc(sizeof(struct petc_input_event), GFP_ATOMIC);
	if (!event) {
		pr_err("[PETC_IF] Out of memory to queue event\n");
		return;
	}

	event->timestamp = timestamp;
	event->type = type;
	event->code = code;
	event->value = value;

	spin_lock_irqsave(&dev_data->event_list_slock, flags);
	list_add_tail(&event->node, &dev_data->event_list);
	spin_unlock_irqrestore(&dev_data->event_list_slock, flags);
}

struct petc_input_event *
petc_if_get_queued_event(struct petc_if_dev_data *dev_data)
{
	struct petc_input_event *event = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev_data->event_list_slock, flags);
	if (!list_empty(&dev_data->event_list)) {
		event = list_first_entry(&dev_data->event_list,
					 struct petc_input_event, node);
		list_del(&event->node);
	}
	spin_unlock_irqrestore(&dev_data->event_list_slock, flags);

	return event;
}

struct petc_input_event *
petc_if_peek_first_queued_event(struct petc_if_dev_data *dev_data)
{
	struct petc_input_event *event = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev_data->event_list_slock, flags);
	if (!list_empty(&dev_data->event_list)) {
		event = list_first_entry(&dev_data->event_list,
					 struct petc_input_event, node);
	}
	spin_unlock_irqrestore(&dev_data->event_list_slock, flags);

	return event;
}

static void petc_if_log_event(ktime_t timestamp, unsigned int type,
			      unsigned int code, int value, bool is_queued_event,
			      bool was_work_pending)
{
	if (type == EV_KEY && code == KEY_POWER) {
		pr_info("[PETC_IF] %s power key event %s: "
			"value=%d timestamp=%lld\n",
			is_queued_event ? "queued" : "dispatched",
			was_work_pending ? "(work was pending)" : "",
			value, ktime_to_ms(timestamp));

	} else {
		pr_debug("[PETC_IF] %s event %s: "
			"type=%d code=%d value=%d timestamp=%lld\n",
			is_queued_event ? "queued" : "dispatched",
			was_work_pending ? "(work was pending)" : "",
			type, code, value, ktime_to_ms(timestamp));
	}
}

static void petc_if_dispatch_queued_event(struct petc_if_dev_data *dev_data,
					  struct petc_input_event *event)
{
	petc_if_log_event(event->timestamp, event->type, event->code, event->value,
		/* is_queued_event */ false,
		/* was_work_pending */ true);
	input_set_timestamp(dev_data->clone_dev, event->timestamp);
	input_event(dev_data->clone_dev, event->type, event->code,
		    event->value);
}

static void petc_if_process_queued_events(struct petc_if_dev_data *dev_data)
{
	struct petc_input_event *event;

	while ((event = petc_if_get_queued_event(dev_data))) {
		if (!petc_if_gating_wheel_events(dev_data->drv_data,
						 event->timestamp) ||
		    event->code != REL_WHEEL) {
			petc_if_dispatch_queued_event(dev_data, event);
		}
		kfree(event);
	}
}

static void petc_if_free_queued_events(struct petc_if_dev_data *dev_data)
{
	struct petc_input_event *event, *next;
	unsigned long flags;

	spin_lock_irqsave(&dev_data->event_list_slock, flags);
	list_for_each_entry_safe(event, next, &dev_data->event_list, node) {
		kfree(event);
	}
	spin_unlock_irqrestore(&dev_data->event_list_slock, flags);
}

static void petc_if_delayed_work_func(struct work_struct *work)
{
	struct petc_if_dev_data *dev_data =
		container_of(work, struct petc_if_dev_data, delayed_work.work);

	pr_debug("[PETC_IF] handling delayed work\n");

	if (!dev_data->clone_registered) {
		pr_warn("[PETC_IF] ignoring work. clone not registered\n");
		return;
	}

	petc_if_process_queued_events(dev_data);
}

static void petc_if_immediate_work_func(struct work_struct *work)
{
	struct petc_input_event *event;
	struct petc_if_dev_data *dev_data =
		container_of(work, struct petc_if_dev_data, immediate_work);
	int mcu_disp_state;

	pr_debug("[PETC_IF] handling immediate work\n");

	if (!dev_data->clone_registered) {
		pr_warn("[PETC_IF] ignoring work. clone not registered\n");
		return;
	}

	event = petc_if_peek_first_queued_event(dev_data);
	if (event == NULL) {
		pr_warn("[PETC_IF] event queue unexpectedly empty\n");
		return;
	}

	mcu_disp_state = mcu_display_query_display_state();
	if (petc_if_is_false_interactive(dev_data, mcu_disp_state)) {
		if (event->type == EV_REL && event->code == REL_WHEEL) {
			pr_info("[PETC_IF] Sending KEY_WAKEUP\n");
			petc_if_send_key(event->timestamp, dev_data, KEY_WAKEUP);
			if (event->value > 0) {
				pr_info("[PETC_IF] KEY_DOWN\n");
				petc_if_send_key(event->timestamp, dev_data, KEY_DOWN);
			} else if (event->value < 0) {
				pr_info("[PETC_IF] KEY_UP\n");
				petc_if_send_key(event->timestamp, dev_data, KEY_UP);
			}
		} else if (event->type == EV_KEY) {
			pr_info("[PETC_IF] Sending KEY_WAKEUP\n");
			petc_if_send_key(event->timestamp, dev_data, KEY_WAKEUP);

			pr_info("[PETC_IF] Queuing key press with code=%u value=%d\n",
				event->code, event->value);
		}

		pr_debug("[PETC_IF] delayed work scheduled\n");
		schedule_delayed_work(&dev_data->delayed_work, WAKEUP_WAIT_MAX);
	} else {
		pr_info("[PETC_IF] NOT petc_if_is_false_interactive\n");
		if (!petc_if_is_ap_screen_on(dev_data->drv_data) &&
		    event->type == EV_REL && event->code == REL_WHEEL &&
		    !petc_if_gating_wheel_events(dev_data->drv_data,
						 event->timestamp)) {
			/* Ignore REL_WHEEL events for GATING_DURATION. */
			dev_data->drv_data->wheel_event_gate_end_time =
				event->timestamp + ms_to_ktime(GATING_DURATION);
			if (!petc_if_is_mcu_screen_off(mcu_disp_state) ||
			     petc_if_is_display_in_transition(dev_data->drv_data,
					mcu_disp_state)) {
				if (event->value > 0) {
					pr_info("[PETC_IF] KEY_DOWN\n");
					petc_if_send_key(event->timestamp, dev_data, KEY_DOWN);
				} else if (event->value < 0) {
					pr_info("[PETC_IF] KEY_UP\n");
					petc_if_send_key(event->timestamp, dev_data, KEY_UP);
				}
			}
			/* Dispatch the peeked event. */
			petc_if_dispatch_queued_event(dev_data, event);
			pr_debug("[PETC_IF] dispatch peeked event value %d\n", event->value);
		}

		/* The peeked event will be removed by
		 * petc_if_process_queued_events().
		 */

		petc_if_process_queued_events(dev_data);
	}
}

static bool petc_if_handle_input(struct petc_if_dev_data *dev_data,
				 ktime_t timestamp, unsigned int type,
				 unsigned int code, int value)
{
	if (!work_pending(&dev_data->immediate_work) &&
		!delayed_work_pending(&dev_data->delayed_work)) {
		if (petc_if_is_ap_screen_on(dev_data->drv_data)) {
			if (!petc_if_gating_wheel_events(dev_data->drv_data, timestamp) ||
			    code != REL_WHEEL) {
				petc_if_log_event(timestamp, type, code, value,
					/* is_queued_event */ false,
					/* was_work_pending */ false);
				input_set_timestamp(dev_data->clone_dev, timestamp);
				input_event(dev_data->clone_dev, type, code, value);
			}
		} else {
			petc_if_log_event(timestamp, type, code, value,
				/* is_queued_event */ true,
				/* was_work_pending */ false);
			petc_if_queue_event(dev_data, timestamp, type, code, value);
			schedule_work(&dev_data->immediate_work);
			pr_debug("[PETC_IF] immediate work scheduled\n");
		}
	} else {
		petc_if_log_event(timestamp, type, code, value,
			/* is_queued_event */ true,
			/* was_work_pending */ true);
		petc_if_queue_event(dev_data, timestamp, type, code, value);
		if (!delayed_work_pending(&dev_data->delayed_work)) {
			pr_debug("[PETC_IF] immediate work scheduled\n");
			schedule_work(&dev_data->immediate_work);
		}
	}
	return true;
}

static void petc_if_register_work_func(struct work_struct *work)
{
	struct petc_if_dev_data *dev_data =
		container_of(work, struct petc_if_dev_data, register_work);
	int error;

	pr_debug("[PETC_IF] handling register work\n");

	if (dev_data->clone_registered) {
		pr_debug("[PETC_IF] clone already registered\n");
		return;
	}

	error = input_register_device(dev_data->clone_dev);
	if (error) {
		pr_err("[PETC_IF] Failed to register clone_dev, error %d\n",
		       error);
		input_unregister_handle(&dev_data->handle);
		input_free_device(dev_data->clone_dev);
		return;
	}

	petc_if_add_device(dev_data->drv_data, dev_data);

	dev_data->clone_registered = true;
}

static void petc_if_remove_device(struct petc_if_dev_data *dev_data)
{
	cancel_work_sync(&dev_data->immediate_work);
	cancel_delayed_work_sync(&dev_data->delayed_work);
	petc_if_free_queued_events(dev_data);
	input_unregister_device(dev_data->clone_dev);
}

struct petc_if_dev_data *
petc_if_get_next_removed_device(struct petc_if_drv_data *drv_data)
{
	struct petc_if_dev_data *dev_data = NULL;
	unsigned long flags;

	spin_lock_irqsave(&drv_data->device_list_slock, flags);
	if (!list_empty(&drv_data->removed_device_list)) {
		dev_data = list_first_entry(&drv_data->removed_device_list,
					    struct petc_if_dev_data, node);
		list_del(&dev_data->node);
	}
	spin_unlock_irqrestore(&drv_data->device_list_slock, flags);

	return dev_data;
}

static void petc_if_remove_devices_work_func(struct work_struct *work)
{
	struct petc_if_dev_data *dev_data;
	struct petc_if_drv_data *drv_data = container_of(
		work, struct petc_if_drv_data, remove_devices_work);

	pr_debug("[PETC_IF] handling remove devices work\n");

	while ((dev_data = petc_if_get_next_removed_device(drv_data))) {
		pr_debug("[PETC_IF] removing device %s\n",
			     dev_data->clone_dev->name);
		petc_if_remove_device(dev_data);
	}
}

/*
 * This is similar to input_get_timestamp(), except that it returns the
 * INPUT_CLK_MONO timestamp instead of INPUT_CLK_REAL.
 * The monotonic timestamp is needed for use with input_set_timestamp().
 */
static ktime_t *petc_if_get_timestamp(struct input_dev *dev)
{
	const ktime_t invalid_timestamp = ktime_set(0, 0);

	if (!ktime_compare(dev->timestamp[INPUT_CLK_MONO], invalid_timestamp))
		input_set_timestamp(dev, ktime_get());

	return &dev->timestamp[INPUT_CLK_MONO];
}

static bool petc_if_filter(struct input_handle *handle, unsigned int type,
			   unsigned int code, int value)
{
	struct petc_if_dev_data *dev_data = handle->private;
	ktime_t *timestamp = petc_if_get_timestamp(handle->dev);

	if (!dev_data->clone_registered) {
		pr_debug("[PETC_IF] input received but clone not yet registered\n");
		return false;
	}

	return petc_if_handle_input(dev_data,
				    *timestamp,
				    type, code, value);
}

struct petc_if_clone_config *petc_if_get_clone_config(struct petc_if_drv_data *drv_data,
					  struct input_dev *input)
{
	struct petc_if_clone_config *config;
	int i;

	if (!input->dev.parent) {
		return NULL;
	}

	for (i = 0; i < drv_data->num_clone_configs; i++) {
		config = &drv_data->clone_configs[i];
		if (input->dev.parent->of_node == config->source_np) {
			return config;
		}
	}

	return NULL;
}

static bool petc_if_match(struct input_handler *handler, struct input_dev *dev)
{
	struct petc_if_drv_data *drv_data =
		(struct petc_if_drv_data *)handler->private;

	if (petc_if_get_clone_config(drv_data, dev) != NULL) {
		return true;
	}

	return false;
}

static int petc_if_open_clone(struct input_dev *dev) {
	struct input_handle *handle =
		(struct input_handle *) input_get_drvdata(dev);
	int error;

	pr_info("[PETC_IF]: Opening clone %s\n", dev->name);
	error = input_open_device(handle);
	if (error) {
		pr_err("[PETC_IF] Failed to open input device, error %d\n",
		       error);
		return error;
	}

	return 0;
}

static void petc_if_close_clone(struct input_dev *dev) {
	struct input_handle *handle =
		(struct input_handle *) input_get_drvdata(dev);

	pr_info("[PETC_IF]: Closing clone %s\n", dev->name);
	input_close_device(handle);
}

static int petc_if_dev_connect(struct input_handler *handler,
			       struct input_dev *dev,
			       const struct input_device_id *id)
{
	struct petc_if_dev_data *dev_data;
	struct petc_if_drv_data *drv_data = handler->private;
	struct input_dev *clone_dev;
	struct device *owner = &drv_data->pdev->dev;
	size_t clone_name_size;
	int error;

	pr_debug("[PETC_IF] device connected\n");

	clone_dev = devm_input_allocate_device(owner);
	if (clone_dev == NULL) {
		pr_err("[PETC_IF] Failed to allocate clone_dev\n");
		return -ENOMEM;
	}

	dev_data = devm_kzalloc(owner, sizeof(struct petc_if_dev_data),
				GFP_KERNEL);
	if (!dev_data) {
		error = -ENOMEM;
		goto err_free_device;
	}

	clone_name_size = strlen(dev->name) + sizeof(PETC_PREFIX);
	dev_data->clone_name = devm_kzalloc(owner, clone_name_size, GFP_KERNEL);
	if (!dev_data->clone_name) {
		pr_err("[PETC_IF] Failed to allocate clone_name\n");
		error = -ENOMEM;
		goto err_free_device;
	}
	strncpy(dev_data->clone_name, PETC_PREFIX, clone_name_size);
	strlcat(dev_data->clone_name, dev->name, clone_name_size);

	clone_dev->name = dev_data->clone_name;
	clone_dev->id.bustype = BUS_VIRTUAL;
	clone_dev->uniq = dev->uniq;
	clone_dev->phys = dev->phys;
	clone_dev->open = petc_if_open_clone;
	clone_dev->close = petc_if_close_clone;

	memcpy(clone_dev->evbit, dev->evbit, sizeof(dev->evbit));
	memcpy(clone_dev->keybit, dev->keybit, sizeof(dev->keybit));
	memcpy(clone_dev->relbit, dev->relbit, sizeof(dev->relbit));
	memcpy(clone_dev->absbit, dev->absbit, sizeof(dev->absbit));
	memcpy(clone_dev->mscbit, dev->mscbit, sizeof(dev->mscbit));
	memcpy(clone_dev->ledbit, dev->ledbit, sizeof(dev->ledbit));
	memcpy(clone_dev->sndbit, dev->sndbit, sizeof(dev->sndbit));
	memcpy(clone_dev->ffbit, dev->ffbit, sizeof(dev->ffbit));
	memcpy(clone_dev->swbit, dev->swbit, sizeof(dev->swbit));
	memcpy(clone_dev->propbit, dev->propbit, sizeof(dev->propbit));

	dev_data->handle.dev = dev;
	dev_data->handle.handler = handler;
	dev_data->handle.name = PETC_HANDLE_NAME;
	dev_data->handle.private = dev_data;
	dev_data->clone_dev = clone_dev;
	dev_data->drv_data = drv_data;

	spin_lock_init(&dev_data->event_list_slock);
	INIT_LIST_HEAD(&dev_data->event_list);
	INIT_WORK(&dev_data->immediate_work, petc_if_immediate_work_func);
	INIT_WORK(&dev_data->register_work, petc_if_register_work_func);
	INIT_DELAYED_WORK(&dev_data->delayed_work, petc_if_delayed_work_func);

	error = input_register_handle(&dev_data->handle);
	if (error) {
		pr_err("[PETC_IF] Failed to register input handle, error %d\n",
		       error);
		goto err_free_device;
	}

	input_set_drvdata(clone_dev, &dev_data->handle);

	/* The device can't be registered here because connect() is called
	 * while input_mutex is held, so queue it for registration.
	 */

	pr_debug("[PETC_IF] work scheduled to register clone device\n");
	schedule_work(&dev_data->register_work);

	return 0;

err_free_device:
	input_free_device(clone_dev);
	return error;
}

static void petc_if_dev_disconnect(struct input_handle *handle)
{
	unsigned long flags;
	struct petc_if_dev_data *dev_data = handle->private;
	struct petc_if_drv_data *drv_data = dev_data->drv_data;

	pr_debug("[PETC_IF] device disconnected\n");

	input_unregister_handle(handle);

	/* The device can't be removed here because disconnect() is called
	 * while input_mutex is held, so queue it for removal.
	 */

	spin_lock_irqsave(&drv_data->device_list_slock, flags);
	list_del(&dev_data->node);
	list_add_tail(&dev_data->node, &drv_data->removed_device_list);
	spin_unlock_irqrestore(&drv_data->device_list_slock, flags);

	schedule_work(&drv_data->remove_devices_work);
}

static const struct input_device_id petc_if_device_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { [BIT_WORD(EV_KEY)] = BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER) },
	}, /* match on devices with a power key */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { [BIT_WORD(EV_KEY)] = BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_VOLUMEDOWN)] = BIT_MASK(KEY_VOLUMEDOWN) },
	}, /* match on devices with a volume down key (resin) */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_RELBIT,
		.evbit = { [BIT_WORD(EV_REL)] = BIT_MASK(EV_REL) },
		.relbit = { [BIT_WORD(REL_WHEEL)] = BIT_MASK(REL_WHEEL) },
	}, /* match on scroll wheels */
	{}, /* Terminating entry */
};

MODULE_DEVICE_TABLE(input, petc_if_device_ids);

void petc_if_display_suspend(struct petc_if_drv_data *drv_data, int ap_disp_state)
{
	pr_debug("[PETC_IF] display suspend\n");
	drv_data->ap_screen = ap_disp_state;
}

void petc_if_display_resume(struct petc_if_drv_data *drv_data, int ap_disp_state)
{
	struct petc_if_dev_data *dev_data;
	unsigned long flags;

	pr_debug("[PETC_IF] display resume\n");
	drv_data->ap_screen = ap_disp_state;

	/* Immediately dispatch queued events on clone devices */

	spin_lock_irqsave(&drv_data->device_list_slock, flags);

	list_for_each_entry(dev_data, &drv_data->device_list, node) {
		if (cancel_delayed_work(&dev_data->delayed_work)) {
			pr_debug(
				"[PETC_IF] delayed work scheduled immediately\n");
			schedule_work(&dev_data->immediate_work);
		}
	}

	spin_unlock_irqrestore(&drv_data->device_list_slock, flags);
}

static void petc_if_drm_panel_notifier_callback(
	enum panel_event_notifier_tag tag,
	struct panel_event_notification *notification, void *data)
{
	struct petc_if_drv_data *drv_data = data;

	if (!notification) {
		return;
	}

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		if (!notification->notif_data.early_trigger) {
			petc_if_display_resume(drv_data,
				notification->notif_type);
		}
		break;

	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_BLANK_LP:
		if (notification->notif_data.early_trigger) {
			petc_if_display_suspend(drv_data,
				notification->notif_type);
		}
		break;
	default:
		break;
	}

	return;
}

static int petc_if_parse_clones(struct petc_if_drv_data *drv_data)
{
	struct device_node *np = drv_data->pdev->dev.of_node;
	struct device_node *source_np;
	int i, count;

	count = of_count_phandle_with_args(np, "clones", NULL);
	if (count <= 0) {
		pr_err("[PETC_IF] failed to find clones, count=%d\n",
		       count);
		return -ENODEV;
	}

	drv_data->clone_configs = devm_kzalloc(&drv_data->pdev->dev,
		sizeof(struct petc_if_clone_config) * count, GFP_KERNEL);
	if (!drv_data->clone_configs) {
		pr_err("[PETC_IF] failed to allocate clone configs, count=%d\n", count);
		return -ENOMEM;
	}

	drv_data->num_clone_configs = count;

	for (i = 0; i < count; i++) {
		source_np = of_parse_phandle(np, "clones", i);
		if (!source_np) {
			pr_err("[PETC_IF] failed to parse clones[%d]\n", i);
			return -ENODEV;
		}
		drv_data->clone_configs[i].source_np = source_np;
		of_node_put(source_np);
	}
	return 0;
}

static int petc_if_register_panel_notifier(struct petc_if_drv_data *drv_data)
{
	struct device_node *np = drv_data->pdev->dev.of_node;
	struct device_node *pnode;
	struct drm_panel *panel;
	void *cookie = NULL;
	int i, count, ret;

	count = of_count_phandle_with_args(np, "display-panels", NULL);
	if (count <= 0) {
		pr_err("[PETC_IF] failed to find display-panels, count=%d\n",
		       count);
		return -ENODEV;
	}

	for (i = 0; i < count; i++) {
		pnode = of_parse_phandle(np, "display-panels", i);
		if (!pnode) {
			pr_err("[PETC_IF] failed to parse display-panels[%d]\n",
			       i);
			return -ENODEV;
		}

		panel = of_drm_find_panel(pnode);
		of_node_put(pnode);
		if (!IS_ERR(panel)) {
			drv_data->active_panel = panel;
			break;
		}
	}

	if (!drv_data->active_panel) {
		pr_err("[PETC_IF] failed to find active panel\n");
		return -ENODEV;
	}

#if defined(CONFIG_DRM_PANEL)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
#error "Registration with drm_panel_notifier_register not implemented"
#else
#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	cookie = panel_event_notifier_register(
		PANEL_EVENT_NOTIFICATION_PRIMARY,
		PANEL_EVENT_NOTIFIER_CLIENT_INPUT_FILTER,
		drv_data->active_panel, &petc_if_drm_panel_notifier_callback,
		drv_data);
	if (IS_ERR(cookie)) {
		ret = PTR_ERR(cookie);
		pr_err("[PETC_IF] failed to register panel event notifier, ret=%d\n",
		       ret);
		return ret;
	}
#endif
#endif
#elif defined(_MSM_DRM_NOTIFY_H_)
#error "Registration with msm_drm_register_client not implemented"
#elif defined(CONFIG_FB)
#error "Registration with fb_register_client not implemented"
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#error "Registration with register_early_suspend not implemented"
#else
#error "Unknown display panel notification registration method"
#endif

	pr_debug("[PETC_IF] register panel notifier successful\n");
	drv_data->notifier_cookie = cookie;
	return 0;
}

static void petc_if_unregister_panel_notifier(struct petc_if_drv_data *drv_data)
{
	if (drv_data->notifier_cookie)
		panel_event_notifier_unregister(drv_data->notifier_cookie);
}

static int petc_if_create_key_dev(struct petc_if_drv_data *drv_data)
{
	struct input_dev *key_dev;
	int error;

	key_dev = devm_input_allocate_device(&drv_data->pdev->dev);
	if (key_dev == NULL) {
		pr_err("[PETC_IF] Failed to allocate key_dev\n");
		return -ENOMEM;
	}

	key_dev->name = "petc";
	key_dev->phys = "input/petc";

	input_set_capability(key_dev, EV_KEY, KEY_WAKEUP);
	input_set_capability(key_dev, EV_KEY, KEY_UP);
	input_set_capability(key_dev, EV_KEY, KEY_DOWN);

	error = input_register_device(key_dev);
	if (error) {
		pr_err("[PETC_IF] Failed to register key_dev, error %d\n",
		       error);
		return error;
	}

	drv_data->key_dev = key_dev;

	return 0;
}

static int petc_if_probe(struct platform_device *pdev)
{
	struct petc_if_drv_data *drv_data;
	struct input_handler *handler;
	int ret;

	pr_debug("[PETC_IF] loading driver\n");

	drv_data = devm_kzalloc(&pdev->dev, sizeof(struct petc_if_drv_data),
				GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	platform_set_drvdata(pdev, drv_data);
	drv_data->pdev = pdev;

	spin_lock_init(&drv_data->device_list_slock);
	INIT_LIST_HEAD(&drv_data->device_list);
	INIT_LIST_HEAD(&drv_data->removed_device_list);
	INIT_WORK(&drv_data->remove_devices_work,
		  petc_if_remove_devices_work_func);

	ret = petc_if_parse_clones(drv_data);
	if (ret) {
		pr_err("[PETC_IF] failed to parse clones in DT, errno:%d\n",
		       ret);
		return ret;
	}

	drv_data->ap_screen = DRM_PANEL_EVENT_UNBLANK;
	ret = petc_if_register_panel_notifier(drv_data);
	if (ret) {
		pr_err("[PETC_IF] failed to register panel notifier, errno:%d\n",
		       ret);
		return ret;
	}

	ret = petc_if_create_key_dev(drv_data);
	if (ret) {
		pr_err("[PETC_IF] failed to create key dev, errno:%d\n", ret);
		goto err_unregister_notifier;
	}

	handler = devm_kzalloc(&pdev->dev, sizeof(struct petc_if_drv_data),
				GFP_KERNEL);
	if (!handler) {
		return -ENOMEM;
	}

	handler->filter = petc_if_filter;
	handler->match = petc_if_match;
	handler->connect = petc_if_dev_connect;
	handler->disconnect = petc_if_dev_disconnect;
	handler->name = PETC_DEV_NAME;
	handler->id_table = petc_if_device_ids;
	handler->private = drv_data;
	drv_data->handler = handler;

	ret = input_register_handler(handler);
	if (ret) {
		pr_err("[PETC_IF] failed to register input handler, errno:%d\n",
		       ret);
		goto err_unregister_notifier;
	}

	return 0;

err_unregister_notifier:
	petc_if_unregister_panel_notifier(drv_data);
	return ret;
}

static void petc_if_remove(struct platform_device *pdev)
{
	struct petc_if_drv_data *drv_data = platform_get_drvdata(pdev);

	pr_debug("[PETC_IF] unloading driver\n");

	input_unregister_handler(drv_data->handler);
	petc_if_unregister_panel_notifier(drv_data);
	flush_work(&drv_data->remove_devices_work);
}

static const struct of_device_id petc_if_dt_match[] = {
	{ .compatible = "google,petc_if" },
	{},
};
MODULE_DEVICE_TABLE(of, petc_if_match);

static struct platform_driver petc_if_driver = {
	.driver = {
		.name = PETC_DEV_NAME,
		.of_match_table = of_match_ptr(petc_if_dt_match),
	},
	.probe = petc_if_probe,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0))
	.remove_new = petc_if_remove
#else
	.remove = petc_if_remove
#endif
};

module_platform_driver(petc_if_driver);

MODULE_AUTHOR("Google");
MODULE_DESCRIPTION("Google PETC input filter driver");
MODULE_LICENSE("GPL");
