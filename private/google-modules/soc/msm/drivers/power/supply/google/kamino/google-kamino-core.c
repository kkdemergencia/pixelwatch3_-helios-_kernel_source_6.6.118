/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Google LLC */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/seq_buf.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <misc/gvotable.h>
#include "google-kamino.h"
#include "google-kamino-event-faker.h"
#include "google-kamino-extcon.h"
#include "google-kamino-moisture-detect-mitigation.h"
#include "google-kamino-periodic-reauth.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#define STATUS_CHANGE_VOTER	"STATUS_CHANGE_VOTER"

#define CHG_MAX_MA		1500
#define FAST_CHG_DEFAULT_MA 	CHG_MAX_MA
#define SLOW_CHG_DEFAULT_MA 	500

const char *google_kamino_status_str(enum kamino_status status)
{
	switch (status) {
	case KAMINO_FAULT_INVALID_CHG:
		return "Invalid charger";
	case KAMINO_FAULT_MOISTURE:
		return "Moisture detected";
	case KAMINO_CHG_FAST:
		return "Fast charging";
	case KAMINO_CHG_SLOW:
		return "Slow charging";
	}
}


bool google_kamino_psy_prop_present(struct kamino *kamino)
{
	union power_supply_propval val;
	enum power_supply_property prop;
	const char *propname;

	if (kamino->psy_online_as_present) {
		prop = POWER_SUPPLY_PROP_ONLINE;
		propname = "online";
	} else {
		prop = POWER_SUPPLY_PROP_PRESENT;
		propname = "present";
	}

	int ret = power_supply_get_property(kamino->psy, prop, &val);
	if (ret < 0)
		dev_err(kamino->dev, "Could not get PSY %s: %d\n",
			propname, ret);

	return (ret == 0) && val.intval;
}

int google_kamino_psy_prop_voltage(struct kamino *kamino, int *uv)
{
	union power_supply_propval val;
	int ret;

	if (!uv)
		return -EINVAL;

	ret = power_supply_get_property(kamino->psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (ret < 0)
		return ret;

	*uv = val.intval;
	return ret;
}

static enum kamino_status read_status(struct kamino *kamino)
{
	struct gpio_descs *gpios = kamino->gpios_status;
	unsigned long values = 0;
	int ret;

	ret = gpiod_get_array_value(gpios->ndescs, gpios->desc,
				gpios->info, &values);

	if (ret < 0)
		return ret;

	switch (values) {
	case KAMINO_FAULT_INVALID_CHG:
	case KAMINO_FAULT_MOISTURE:
	case KAMINO_CHG_FAST:
	case KAMINO_CHG_SLOW:
		return (enum kamino_status)values;
	default:
		dev_err(kamino->dev, "Invalid status values: %lu\n", values);
		return KAMINO_FAULT_INVALID_CHG;
	}
}

static int set_current_max(struct kamino *kamino, int current_ma)
{
	union power_supply_propval val;
	int ret;

	/* Convert to uA */
	val.intval = current_ma * 1000;
	dev_info(kamino->dev, "Setting max current to %d uA", val.intval);

	ret = power_supply_set_property(kamino->psy,
					POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
					&val);
	if (ret < 0) {
		dev_err(kamino->dev,
			"Could not set max current on psy %s: %d\n",
			kamino->psy->desc->name, ret);
	}

	return ret;
}

static int vote_cb_notify_icl_changed(struct gvotable_election *el,
				const char *reason,
				void *vote)
{
	struct kamino *kamino = (struct kamino *)gvotable_get_data(el);
	dev_dbg(kamino->dev, "Got vote, ICL: %d\n", GVOTABLE_PTR_TO_INT(vote));
	return set_current_max(kamino, GVOTABLE_PTR_TO_INT(vote));
}

static int vote_cb_fake_psy_present_online_changed(struct gvotable_election *el,
						const char *reason,
						void *vote)
{
	struct kamino *kamino = (struct kamino*) gvotable_get_data(el);
	union power_supply_propval val;
	int ret;

	val.intval = GVOTABLE_PTR_TO_INT(vote);

	dev_dbg(kamino->dev, "Got vote, fake: %s\n", val.intval ? "yes" : "no");

	/*
	 * If we are using the PRESENT prop, we must fake it as well as the
	 * ONLINE prop
	 */
	if (!kamino->psy_online_as_present) {
		ret = power_supply_set_property(kamino->psy,
						POWER_SUPPLY_PROP_PRESENT,
						&val);
		if (ret < 0) {
			dev_warn(kamino->dev,
				"Could not fake PSY present: %d\n", ret);
			return ret;
		}
	}

	ret = power_supply_set_property(kamino->psy, POWER_SUPPLY_PROP_ONLINE,
					&val);
	if (ret < 0)
		dev_warn(kamino->dev, "Could not fake PSY online: %d\n", ret);

	return ret;
}

static void psy_changed_work(struct work_struct *work)
{
	struct kamino *kamino = container_of(work, struct kamino,
					psy_changed_work);
	google_kamino_moisture_detect_mitigation_psy_changed(kamino);
	google_kamino_periodic_reauth_psy_changed(kamino);
	google_kamino_extcon_psy_changed(kamino);
}

static int psy_changed_notifier_cb(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct kamino *kamino = container_of(nb, struct kamino, psy_changed_nb);

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if (!strcmp(psy->desc->name, kamino->psy->desc->name))
		queue_work(kamino->q, &kamino->psy_changed_work);

	return NOTIFY_OK;
}

#define UEVENT_MSG_LEN 32
static void publish_status_change_uevent(struct kamino *kamino,
		const enum kamino_status status)
{
	int ret;
	char uevent_msg[UEVENT_MSG_LEN];
	char *uevent_env[2] = {uevent_msg, NULL};

	scnprintf(uevent_msg, UEVENT_MSG_LEN, "STATUS=%s",
		google_kamino_status_str(status));
	ret = kobject_uevent_env(&kamino->dev->kobj, KOBJ_CHANGE, uevent_env);
	if (ret != 0) {
		dev_warn(kamino->dev, "Error sending status uevent: %d\n", ret);
	}
}

static void populate_event(struct kamino *kamino, struct kamino_event *out)
{
	out->time = google_kamino_gettime();
	/*
	 * NOTE: These events typically get created in a threaded interrupt
	 *       handler, whereas the reauth runs in a separate thread.
	 *       This means that there is a possibility of misattributing an
	 *       event to the reauth, since the reauth thread may say reauth is
	 *       occurring, then get scheduled out before any actual reauth
	 *       steps run. While the thread is suspended, kamino may trigger a
	 *       new status.
	 *
	 *       Since kamino runs autonomously, and since we can only disable
	 *       interrupts on our side rather than on the device, we cannot
	 *       ever know whether an interrupt was definitively caused by the
	 *       reauth. In practice this means that if a user is handling the
	 *       device, if they cause a status change, we may misattribute the
	 *       event to the reauth.
	 *
	 *       At this time, we only care about reauth for the moisture
	 *       status, as there is some possibility of the user triggering a
	 *       false moisture status if they interact with the device.
	 *
	 *       However, given the fact that there is a low chance of the
	 *       false positive happening, and that the timing would then have
	 *       to be such that the user interacts with the device to cause
	 *       a false positive at the same time reauth is running, it
	 *       further reduces the chances of us attributing a false positive
	 *       moisture event to the reauth.
	 *
	 *       Therefore, this behaviour is acceptable.
	 */
	out->in_reauth = google_kamino_periodic_reauth_in_progress(kamino);
	out->status = read_status(kamino);
}

static inline void copy_event(struct kamino_event *dest,
			struct kamino_event *src)
{
	memcpy(dest, src, sizeof(*dest));
}

static int copy_event_to_timeline(struct kamino *kamino,
			struct kamino_event_timeline *timeline,
			struct kamino_event *event)
{
	int ret = 0;
	struct kamino_event_item *item = NULL;

	mutex_lock(&timeline->lock);
	if (timeline->num == KAMINO_NUM_EVENTS_MAX) {
		item = list_last_entry(&timeline->head,
				struct kamino_event_item, elem);
		list_rotate_to_front(&item->elem, &timeline->head);
	} else {
		item = &timeline->free_items[timeline->num];
		list_add(&item->elem, &timeline->head);
		timeline->num++;
	}
	copy_event(&item->event, event);

	mutex_unlock(&timeline->lock);
	return ret;
}

static void status_change_work(struct work_struct *work)
{
	struct kamino *kamino = container_of(work, struct kamino,
					status_change_work);
	struct kamino_event event;
	struct kamino_event_item *tmp;
	struct kamino_event override;
	int icl;
	int ret = 0;

	mutex_lock(&kamino->event_timeline.lock);
	tmp = list_first_entry_or_null(&kamino->event_timeline.head,
				struct kamino_event_item, elem);
	if (tmp)
		copy_event(&event, &tmp->event);
	else
		ret = -EAGAIN;

	mutex_unlock(&kamino->event_timeline.lock);

	if (ret != 0) {
		dev_err(kamino->dev, "No events to process!\n");
		return;
	}

	ret = google_kamino_moisture_detect_mitigation_override(kamino,
								&override);
	if (ret == 0)
		copy_event(&event, &override);
	else if ((ret == -ENODATA) && (event.status == KAMINO_FAULT_MOISTURE))
		return;

	/*
	 * Make new status visible to other CPUs in case one of them is
	 * polling on it.
	 */
	smp_store_mb(kamino->active_status, event.status);

	dev_info(kamino->dev, "Active status: %s @%lld ms%s\n",
		google_kamino_status_str(event.status), ktime_to_ms(event.time),
		(ret == 0) ? " (override)" : "");

	switch (kamino->active_status) {
	case KAMINO_FAULT_INVALID_CHG:
		if (kamino->charge_on_invalid_charger) {
			dev_warn(kamino->dev,
				"Configuring invalid charger for slow charging!\n");
			icl = kamino->slow_chg_ma;
		}
		else
			icl = 0;
		break;
	case KAMINO_CHG_FAST:
		icl = kamino->fast_chg_ma;
		break;
	case KAMINO_CHG_SLOW:
		icl = kamino->slow_chg_ma;
		break;
	default:
		icl = 0;
		break;
	}

	ret = gvotable_cast_int_vote(kamino->icl_votable,
				STATUS_CHANGE_VOTER, icl, true);

	if (ret < 0)
		dev_err(kamino->dev,
			"Could not vote for max current: %d\n", ret);

	publish_status_change_uevent(kamino, kamino->active_status);
	google_kamino_extcon_kamino_active_status_changed(kamino);
}

void google_kamino_on_new_event(struct kamino *kamino,
				struct kamino_event *event)
{
	const char *status_str = google_kamino_status_str(event->status);
	const char *faked_str = event->faked ? " (faked)" : "";
	const char *reauth_str;

	int ret = copy_event_to_timeline(kamino,
					&kamino->event_timeline, event);
	if (ret != 0) {
		dev_warn(kamino->dev, "Could not add %s @%lld ms to list: %d\n",
			status_str, ktime_to_ms(event->time), ret);
		return;
	}

	google_kamino_moisture_detect_mitigation_on_new_status(kamino,
							event->status,
							event->in_reauth);

	reauth_str = event->in_reauth ? " (possible reauth)" : "";
	dev_info(kamino->dev, "New status: %s%s%s\n", status_str,
		reauth_str, faked_str);

	queue_work(kamino->q, &kamino->status_change_work);
}

void google_kamino_on_new_status(struct kamino *kamino)
{
	struct kamino_event event;

	populate_event(kamino, &event);
	google_kamino_on_new_event(kamino, &event);
}

static irqreturn_t status_change_handler(int irq, void *data)
{
	google_kamino_on_new_status((struct kamino *)data);
	return IRQ_HANDLED;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	enum kamino_status status = smp_load_acquire(&kamino->active_status);
	return sysfs_emit(buf, "%s\n", google_kamino_status_str(status));
}
static const DEVICE_ATTR_RO(status);

static ssize_t recent_statuses_show(struct device *dev,
				struct device_attribute *attr,
				char *mem)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	struct kamino_event_timeline *timeline = &kamino->event_timeline;
	struct kamino_event_item *tmp;
	struct seq_buf buf;
	int ret = 0;

	seq_buf_init(&buf, mem, PAGE_SIZE);

	/* Note: We hold the mutex while we iterate over the entire timeline */
	mutex_lock(&timeline->lock);

	if (list_empty(&timeline->head)) {
		ret = -ENODATA;
		goto out;
	}

	seq_buf_printf(&buf, "[%lld ms] **NOW**\n",
		ktime_to_ms(google_kamino_gettime()));

	list_for_each_entry(tmp, &timeline->head, elem) {
		const char *reauth_str =
			tmp->event.in_reauth ? " (possible reauth)" : "";
		const char *faked_str =
			tmp->event.faked ? " (faked)" : "";

		seq_buf_printf(&buf, "[%lld ms] %s%s%s\n",
				ktime_to_ms(tmp->event.time),
				google_kamino_status_str(tmp->event.status),
				reauth_str, faked_str);
	}
out:
	mutex_unlock(&timeline->lock);

	if (ret != 0)
		return ret;

	if (seq_buf_has_overflowed(&buf))
		return -EOVERFLOW;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0))
	seq_buf_terminate(&buf);
#else
	seq_buf_str(&buf);
#endif

	return seq_buf_used(&buf);
}
static const DEVICE_ATTR_RO(recent_statuses);

static ssize_t heartbeat_en_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	bool enable;
	int ret;

	if (!kamino->gpio_heartbeat_en)
		return -ENOTSUPP;

	ret = kstrtobool(buf, &enable);
	if (ret != 0)
		return ret;

	gpiod_set_value(kamino->gpio_heartbeat_en, enable);

	return count;
}

static ssize_t heartbeat_en_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	int val;

	if (!kamino->gpio_heartbeat_en)
		return -ENOTSUPP;

	val = gpiod_get_value(kamino->gpio_heartbeat_en);

	return (val < 0) ? val : sysfs_emit(buf, "%u\n", val);
}
static const DEVICE_ATTR_RW(heartbeat_en);

static ssize_t store_chg_current(struct kamino *kamino,
				struct device_attribute *attr,
				const char *buf, size_t count,
				u16 *current_ma)
{
	u16 parsed_ma;
	int ret;

	ret = kstrtou16(buf, 0, &parsed_ma);

	if (ret < 0)
		return ret;

	if ((parsed_ma == 0) || (parsed_ma > CHG_MAX_MA)) {
		return -EINVAL;
	}

	*current_ma = parsed_ma;

	queue_work(kamino->q, &kamino->status_change_work);

	return count;
}

static ssize_t fast_chg_ma_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	return store_chg_current(kamino, attr, buf, count,
				&kamino->fast_chg_ma);
}

static ssize_t fast_chg_ma_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", kamino->fast_chg_ma);
}
static const DEVICE_ATTR_RW(fast_chg_ma);

static ssize_t slow_chg_ma_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	return store_chg_current(kamino, attr, buf, count,
				&kamino->slow_chg_ma);
}

static ssize_t slow_chg_ma_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", kamino->slow_chg_ma);
}
static const DEVICE_ATTR_RW(slow_chg_ma);

static int kamino_create_file(struct device *dev,
			const struct device_attribute *attr)
{
	int ret = device_create_file(dev, attr);
	if (ret < 0)
		dev_err(dev, "Failed to create %s: %d\n", attr->attr.name, ret);

	return ret;
}

static void setup_sysfs(struct kamino *kamino)
{
	(void)kamino_create_file(kamino->dev, &dev_attr_status);
	(void)kamino_create_file(kamino->dev, &dev_attr_recent_statuses);
	(void)kamino_create_file(kamino->dev, &dev_attr_heartbeat_en);
	(void)kamino_create_file(kamino->dev, &dev_attr_fast_chg_ma);
	(void)kamino_create_file(kamino->dev, &dev_attr_slow_chg_ma);
	google_kamino_moisture_detect_mitigation_setup_sysfs(kamino);
}

static void teardown_sysfs(struct kamino *kamino)
{
	google_kamino_moisture_detect_mitigation_teardown_sysfs(kamino);
	device_remove_file(kamino->dev, &dev_attr_status);
	device_remove_file(kamino->dev, &dev_attr_heartbeat_en);
	device_remove_file(kamino->dev, &dev_attr_fast_chg_ma);
	device_remove_file(kamino->dev, &dev_attr_slow_chg_ma);
}

static int setup_gpios(struct kamino *kamino)
{
	kamino->gpio_heartbeat_en =
		devm_gpiod_get_optional(kamino->dev, "google,heartbeat-en",
					GPIOD_OUT_HIGH);
	if (!kamino->gpio_heartbeat_en)
		dev_warn(kamino->dev, "No heartbeat-enable pin\n");

	kamino->gpios_status =
		devm_gpiod_get_array(kamino->dev, "google,status", GPIOD_IN);
	if (IS_ERR(kamino->gpios_status)) {
		dev_err(kamino->dev, "Failed to get status GPIOs: %ld\n",
			PTR_ERR(kamino->gpios_status));
		return PTR_ERR(kamino->gpios_status);
	}

	return 0;
}

static int setup_status_irq(struct kamino *kamino,  unsigned int idx,
			struct gpio_desc *desc, unsigned int *irq_out)
{
	int irq;
	int ret;
	char *name;

	irq = gpiod_to_irq(desc);
	if (irq < 0) {
		dev_err(kamino->dev, "Failed to get status %u IRQ num: %d\n",
			idx, irq);
		return irq;
	}

	name = devm_kasprintf(kamino->dev, GFP_KERNEL,
			"kamino-status-%u", (idx + 1));
	if (!name)
		return -ENOMEM;

	ret = devm_request_threaded_irq(kamino->dev, irq, NULL,
					status_change_handler,
					IRQF_ONESHOT
					| IRQF_TRIGGER_RISING
					| IRQF_TRIGGER_FALLING,
					name, kamino);
	if (ret != 0) {
		dev_err(kamino->dev, "Failed to request status %u IRQ: %d\n",
			idx, ret);
		return ret;
	}

	*irq_out = irq;

	return 0;
}

static int setup_irqs(struct kamino *kamino)
{
	int i;

	kamino->status_irqs = devm_kcalloc(kamino->dev,
					kamino->gpios_status->ndescs,
					sizeof(*kamino->status_irqs),
					GFP_KERNEL);
	if (!kamino->status_irqs)
		return -ENOMEM;

	for (i = 0; i < kamino->gpios_status->ndescs; i++) {
		int ret = setup_status_irq(kamino, i,
					kamino->gpios_status->desc[i],
					&kamino->status_irqs[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

void google_kamino_disable_irqs(struct kamino *kamino)
{
	int i;

	for (i = 0; i < kamino->gpios_status->ndescs; i++) {
		if (kamino->status_irqs[i])
			disable_irq(kamino->status_irqs[i]);
	}
}

void google_kamino_enable_irqs(struct kamino *kamino)
{
	int i;

	for (i = 0; i < kamino->gpios_status->ndescs; i++) {
		if (kamino->status_irqs[i])
			enable_irq(kamino->status_irqs[i]);
	}
}

static void set_irqs_wake(struct kamino *kamino, bool wake)
{
	int i;

	for (i = 0; i < kamino->gpios_status->ndescs; i++) {
		int irq = kamino->status_irqs[i];

		if (irq) {
			int ret;

			if (wake)
				ret = enable_irq_wake(irq);
			else
				ret = disable_irq_wake(irq);

			if (ret < 0) {
				dev_warn(kamino->dev,
					"Could not %s IRQ %d: %d\n",
					(wake ? "enable" : "disable"),
					irq, ret);
			}
		}
	}
}

static struct power_supply *find_psy(struct device *dev)
{
	int ret;
	struct device_node *node = dev->of_node;
	const char *prop_name = "google,psy-name";
	const char *psy_name;

	ret = of_property_read_string(node, prop_name, &psy_name);
	if (ret != 0) {
		dev_err(dev, "Could not read %s string: %d\n", prop_name, ret);
		return NULL;
	}

	return power_supply_get_by_name(psy_name);
}

void init_chg_rate(struct kamino *kamino, const char *propname,
		u16 *current_ma_out, u16 default_ma)
{
	struct device_node *node = kamino->dev->of_node;
	int ret = of_property_read_u16(node, propname, current_ma_out);

	if (ret < 0) {
		dev_warn(kamino->dev,
			"No \'%s\' chg rate prop found, using %u as default\n",
			propname, default_ma);
		*current_ma_out = default_ma;
	}
}

void init_chg_rates(struct kamino *kamino)
{
	init_chg_rate(kamino, "google,fast-chg-ma", &kamino->fast_chg_ma,
		FAST_CHG_DEFAULT_MA);
	init_chg_rate(kamino, "google,slow-chg-ma", &kamino->slow_chg_ma,
		SLOW_CHG_DEFAULT_MA);
}

#ifdef CONFIG_DEBUG_FS
static void setup_debugfs(struct kamino *kamino, const char *dirname)
{
	kamino->debugfs_dir = debugfs_create_dir(dirname, NULL);
	if (IS_ERR_OR_NULL(kamino->debugfs_dir)) {
		kamino->debugfs_dir = NULL;
		return;
	}

	google_kamino_moisture_detect_mitigation_setup_debugfs(kamino);
	google_kamino_periodic_reauth_setup_debugfs(kamino);
	google_kamino_extcon_setup_debugfs(kamino);
	google_kamino_event_faker_setup_debugfs(kamino);
}
#else
static void setup_debugfs(struct kamino *kamino, const char *dirname) {}
#endif

static int kamino_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kamino *kamino;
	struct power_supply *psy;
	int ret;

	dev_dbg(dev, "Probe start\n");

	psy = find_psy(dev);
	if (!psy) {
		dev_err(dev, "Could not find power supply\n");
		return -EPROBE_DEFER;
	}

	kamino = devm_kzalloc(&pdev->dev, sizeof(*kamino), GFP_KERNEL);
	if (!kamino)
		return -ENOMEM;

	kamino->q = alloc_ordered_workqueue("goog-kamino", 0);
	if (!kamino->q) {
		dev_err(&pdev->dev, "Could not alloc wq!\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, kamino);
	dev_set_drvdata(dev, kamino);

	kamino->dev = dev;
	kamino->psy = psy;
	kamino->psy_online_as_present =
		of_property_read_bool(kamino->dev->of_node,
				"google,psy-online-as-present");

	INIT_LIST_HEAD(&kamino->event_timeline.head);
	mutex_init(&kamino->event_timeline.lock);

	INIT_WORK(&kamino->status_change_work, status_change_work);

	init_chg_rates(kamino);

	kamino->charge_on_invalid_charger =
		of_property_read_bool(kamino->dev->of_node,
				"google,charge-on-invalid-charger");

	ret = device_init_wakeup(dev, true /*wakeup*/);
	if (ret < 0) {
		dev_err(dev, "Could not enable wakeup: %d\n", ret);
		/* Nonfatal */
		ret = 0;
	}

	ret = setup_gpios(kamino);
	if (ret < 0)
		goto fail;

	kamino->icl_votable =
		gvotable_create_int_election("KAMINO_ICL",
					gvotable_comparator_uint_min,
					vote_cb_notify_icl_changed,
					kamino);
	if (IS_ERR_OR_NULL(kamino->icl_votable)) {
		ret = -ENOMEM;
		goto fail;
	}
	gvotable_set_vote2str(kamino->icl_votable, gvotable_v2s_int);

	kamino->fake_psy_present_online_votable =
		gvotable_create_bool_election("KAMINO_PSY_FAKE",
					vote_cb_fake_psy_present_online_changed,
					kamino);
	if (IS_ERR_OR_NULL(kamino->fake_psy_present_online_votable)) {
		ret = -ENOMEM;
		goto fail_with_icl_votable;
	}
	gvotable_set_vote2str(kamino->fake_psy_present_online_votable,
			gvotable_v2s_uint);

	ret = google_kamino_moisture_detect_mitigation_init(kamino);
	if (ret < 0)
		goto fail_with_psy_fake_votable;

	ret = setup_irqs(kamino);
	if (ret < 0)
		goto fail_with_mitigation;

	smp_store_release(&kamino->active_status, KAMINO_CHG_SLOW);

	kamino->psy_link = device_link_add(kamino->dev, &kamino->psy->dev,
					DL_FLAG_STATELESS);

	ret = google_kamino_periodic_reauth_init(kamino);
	if (ret < 0)
		goto fail_with_irq;

	ret = google_kamino_extcon_init(kamino);
	if ((ret < 0) && (ret != -ENOTSUPP))
		goto fail_with_reauth;

	google_kamino_event_faker_init(kamino);

	/*
	 * There may not be any IRQs that need to be handled, so create an
	 * event and send it off for processing
	 */
	google_kamino_on_new_status(kamino);

	INIT_WORK(&kamino->psy_changed_work, psy_changed_work);
	kamino->psy_changed_nb.notifier_call = psy_changed_notifier_cb;
	power_supply_reg_notifier(&kamino->psy_changed_nb);

	schedule_work(&kamino->psy_changed_work);

	setup_sysfs(kamino);
	setup_debugfs(kamino, pdev->name);

	return 0;

fail_with_psy_fake_votable:
	gvotable_destroy_election(kamino->fake_psy_present_online_votable);
fail_with_icl_votable:
	gvotable_destroy_election(kamino->icl_votable);
fail_with_reauth:
	google_kamino_periodic_reauth_deinit(kamino);
fail_with_mitigation:
	google_kamino_moisture_detect_mitigation_deinit(kamino);
fail_with_irq:
	google_kamino_disable_irqs(kamino);
fail:
	destroy_workqueue(kamino->q);
	return ret;
}

static void kamino_remove(struct platform_device *pdev)
{
	struct kamino *kamino = platform_get_drvdata(pdev);
	debugfs_remove_recursive(kamino->debugfs_dir);
	teardown_sysfs(kamino);
	power_supply_unreg_notifier(&kamino->psy_changed_nb);
	cancel_work_sync(&kamino->psy_changed_work);
	google_kamino_event_faker_deinit(kamino);
	google_kamino_extcon_deinit(kamino);
	google_kamino_periodic_reauth_deinit(kamino);
	google_kamino_disable_irqs(kamino);
	cancel_work_sync(&kamino->status_change_work);
	google_kamino_moisture_detect_mitigation_deinit(kamino);
	gvotable_destroy_election(kamino->fake_psy_present_online_votable);
	gvotable_destroy_election(kamino->icl_votable);
	device_init_wakeup(kamino->dev, false /*disable wakeup*/);
	if (kamino->psy_link)
		device_link_del(kamino->psy_link);
	power_supply_put(kamino->psy);
	mutex_destroy(&kamino->event_timeline.lock);
	dev_set_drvdata(&pdev->dev, NULL);
	destroy_workqueue(kamino->q);
	platform_set_drvdata(pdev, NULL);
}

static int kamino_pm_suspend(struct device *dev)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	set_irqs_wake(kamino, true /*wake*/);
	google_kamino_periodic_reauth_pm_suspend(kamino);
	flush_workqueue(kamino->q);
	return 0;
}

static int kamino_pm_resume(struct device *dev)
{
	struct kamino *kamino = dev_get_drvdata(dev);
	set_irqs_wake(kamino, false /*no wake*/);
	return 0;
}

static const struct dev_pm_ops kamino_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(kamino_pm_suspend, kamino_pm_resume)
};

static const struct of_device_id match_table[] = {
	{ .compatible = "google,kamino" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, match_table);
static struct platform_driver kamino_driver = {
	.driver	= {
		.name = "google-kamino",
		.of_match_table	= of_match_ptr(match_table),
		.pm = &kamino_pm_ops,
	},
	.probe	= kamino_probe,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0))
	.remove_new = kamino_remove,
#else
	.remove	= kamino_remove,
#endif
};
module_platform_driver(kamino_driver);
MODULE_DESCRIPTION("Google Kamino Driver");
MODULE_LICENSE("GPL v2");
