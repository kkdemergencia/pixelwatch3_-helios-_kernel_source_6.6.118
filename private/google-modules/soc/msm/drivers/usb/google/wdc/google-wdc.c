/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Google LLC */

#include <linux/device.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#define DEFAULT_LINK_LOST_DEBOUNCE_MS 1000U;
#define EXTCON_ID_EXT_CTRL EXTCON_CHG_USB_FAST
#define MAX_LOAD_UA 250000
#define VDD_UV 1800000

static const unsigned int supported_cables[] = {
	EXTCON_USB,
	EXTCON_NONE /* sentinel */
};

struct wdc_ext_ctrl {
	struct extcon_dev *extcon;
	struct notifier_block nb;
	struct work_struct work;
	bool en;
};

struct wdc {
	struct mutex lock;
	struct device *dev;
	struct regulator *vdd;
	struct delayed_work status_update_work;
	unsigned int link_lost_debounce_ms;
	struct gpio_desc *gpio_link_status;
	struct gpio_desc *gpio_enable;
	unsigned int irq;
	struct extcon_dev *extcon;
	struct wdc_ext_ctrl ext_ctrl;
	bool is_enabled;
};

static inline bool has_link(struct wdc *wdc) {
	return gpiod_get_value(wdc->gpio_link_status) != 0;
}

static int vdd_enable(struct wdc *wdc, bool enable)
{
	const char *action = enable ? "enable" : "disable";
	int ret;

	dev_info(wdc->dev, "VDD %s\n", action);

	if (enable)
		ret = regulator_enable(wdc->vdd);
	else
		ret = regulator_disable(wdc->vdd);

	if (ret < 0)
		dev_err(wdc->dev, "Failed to %s VDD: %d\n", action, ret);

	return ret;
}

static void enable(struct wdc *wdc, bool enable)
{
	int ret;
	const char *action = enable ? "enable" : "disable";

	if (wdc->is_enabled == enable)
		return;

	/*
	 * Turn VDD on before enabling WDC,
	 * but turn it off after disabling WDC
	 */
	if (enable) {
		ret = vdd_enable(wdc, true);
		if (ret < 0)
			return;
	}

	dev_info(wdc->dev, "%s\n", action);
	gpiod_set_value(wdc->gpio_enable, enable);

	if (!enable)
		(void)vdd_enable(wdc, false);

	wdc->is_enabled = enable;
}

static void do_status_update(struct wdc *wdc)
{
	int ret;
	bool link = has_link(wdc);

	dev_info(wdc->dev, "Updating extcon to: %s\n",
		link ? "connected" : "disconnected");

	ret = extcon_set_state_sync(wdc->extcon, EXTCON_USB, link);
	if (ret < 0)
		dev_err(wdc->dev, "Could not set extcon state: %d\n", ret);
}

static void status_update_work(struct work_struct *work)
{
	struct wdc *wdc = container_of(to_delayed_work(work),
				struct wdc, status_update_work);
	do_status_update(wdc);
}

static void controller_change_work(struct work_struct *work)
{
	struct wdc_ext_ctrl *ctrl = container_of(work,
						struct wdc_ext_ctrl, work);
	struct wdc *wdc = container_of(ctrl, struct wdc, ext_ctrl);
	bool en = (extcon_get_state(ctrl->extcon, EXTCON_ID_EXT_CTRL) != 0);

	dev_info(wdc->dev, "Controller: %d\n", en);
	enable(wdc, en);
}

static int controller_change_notify(struct notifier_block *notifier,
				unsigned long on, void *unused)
{
	struct wdc_ext_ctrl *ctrl = container_of(notifier,
						struct wdc_ext_ctrl, nb);
	schedule_work(&ctrl->work);
	return NOTIFY_DONE;
}

static irqreturn_t link_status_handler(int irq, void *data)
{
	struct wdc *wdc = data;
	bool link = has_link(wdc);
	/*
	 * We assume that if the link status went up it was legitimate, so
	 * no need to debounce it.
	 * On the other hand, for cases like USB tunnelling where we only enable
	 * the USB PHY once we have link status, if the HW takes a longer time
	 * to start up the host's WDC may see the lack of USB communication as
	 * problematic and reset the link. This has the possibility of creating
	 * a loop where the link continually resets since the HW will never be
	 * ready in time. Therefore, if we delay before reporting the link has
	 * gone down, the HW stays enabled and (provided the debounce is long
	 * enough) will be ready to communicate immediately once the host resets
	 * the link.
	 */
	int debounce_ms = link ? 0 : wdc->link_lost_debounce_ms;

	dev_dbg(wdc->dev, "Raw link status: %s\n",
		link ? "connected" : "disconnected");

	pm_wakeup_dev_event(wdc->dev, debounce_ms + 100, true);

	cancel_delayed_work(&wdc->status_update_work);
	schedule_delayed_work(&wdc->status_update_work,
			msecs_to_jiffies(debounce_ms));

	return IRQ_HANDLED;
}

static int ext_ctrl_en(struct wdc *wdc, bool en)
{
	struct wdc_ext_ctrl *ctrl = &wdc->ext_ctrl;
	int ret;

	if (IS_ERR(ctrl->extcon)) {
		return -ENOTSUPP;
	}

	if (ctrl->en == en)
		return 0;

	if (en) {
		ret = extcon_register_notifier(ctrl->extcon, EXTCON_ID_EXT_CTRL,
					&ctrl->nb);
		if (ret < 0) {
			dev_err(wdc->dev, "Failed to register ctl nb: %d\n",
				ret);
			enable(wdc, true);
		}
	} else {
		ret = extcon_unregister_notifier(ctrl->extcon,
						EXTCON_ID_EXT_CTRL, &ctrl->nb);
	}

	if (ret == 0)
		ctrl->en = en;

	if (ctrl->en)
		schedule_work(&wdc->ext_ctrl.work);

	return ret;
}

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct wdc *wdc = dev_get_drvdata(dev);
	int val = gpiod_get_value(wdc->gpio_enable);

	if (val < 0)
		return val;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t enabled_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct wdc *wdc = dev_get_drvdata(dev);
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret != 0)
		return ret;

	enable(wdc, en);

	return count;
}
static const DEVICE_ATTR_RW(enabled);

static ssize_t link_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct wdc *wdc = dev_get_drvdata(dev);
	int val = gpiod_get_value(wdc->gpio_link_status);

	if (val < 0)
		return val;

	return sysfs_emit(buf, "%d\n", val);
}
static const DEVICE_ATTR_RO(link_status);

static ssize_t ext_ctrl_en_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct wdc *wdc = dev_get_drvdata(dev);
	bool en;

	mutex_lock(&wdc->lock);
	en = wdc->ext_ctrl.en;
	mutex_unlock(&wdc->lock);

	return sysfs_emit(buf, "%u\n", en);
}

static ssize_t ext_ctrl_en_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct wdc *wdc = dev_get_drvdata(dev);
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret != 0)
		return ret;

	mutex_lock(&wdc->lock);
	ret = ext_ctrl_en(wdc, en);
	mutex_unlock(&wdc->lock);

	return (ret == 0) ? count : ret;
}
static const DEVICE_ATTR_RW(ext_ctrl_en);

static int wdc_create_file(struct device *dev,
			const struct device_attribute *attr)
{
	int ret = device_create_file(dev, attr);
	if (ret < 0)
		dev_err(dev, "Failed to create %s: %d\n", attr->attr.name, ret);

	return ret;
}

static void setup_sysfs(struct wdc *wdc)
{
	int ret;
	(void)wdc_create_file(wdc->dev, &dev_attr_link_status);
	(void)wdc_create_file(wdc->dev, &dev_attr_enabled);
	ret = wdc_create_file(wdc->dev, &dev_attr_ext_ctrl_en);
	if (ret < 0) {
		dev_err(wdc->dev, "Forcing WDC on\n");
		enable(wdc, true);
	}
}

static void teardown_sysfs(struct wdc *wdc)
{
	device_remove_file(wdc->dev, &dev_attr_link_status);
	device_remove_file(wdc->dev, &dev_attr_enabled);
	device_remove_file(wdc->dev, &dev_attr_ext_ctrl_en);
}

static int setup_gpios(struct wdc *wdc)
{
	enum gpiod_flags enable_init_val;
	bool start_enabled;

	wdc->gpio_link_status = devm_gpiod_get(wdc->dev, "google,link-status",
					GPIOD_IN);
	if (IS_ERR(wdc->gpio_link_status)) {
		dev_err(wdc->dev, "Failed to get link status GPIO: %ld\n",
			PTR_ERR(wdc->gpio_link_status));
		return PTR_ERR(wdc->gpio_link_status);
	}

	start_enabled = of_property_read_bool(wdc->dev->of_node,
					"google,start-enabled");

	if (start_enabled) {
		int ret = vdd_enable(wdc, true);

		if (ret < 0) {
			/*
			 * Allow initialization to complete, in case the
			 * error is temporary, but skip starting with WDC on.
			 */
			start_enabled = false;
		}
	}

	enable_init_val = start_enabled ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;

	wdc->gpio_enable = devm_gpiod_get(wdc->dev, "google,enable",
					enable_init_val);
	if (IS_ERR(wdc->gpio_enable)) {
		dev_err(wdc->dev, "Failed to get enable-control GPIO: %ld\n",
			PTR_ERR(wdc->gpio_enable));
		return PTR_ERR(wdc->gpio_enable);
	}

	wdc->is_enabled = start_enabled;

	return 0;
}

static int setup_link_status_irq(struct wdc *wdc)
{
	int ret;

	ret = gpiod_to_irq(wdc->gpio_link_status);
	if (ret < 0) {
		dev_err(wdc->dev, "Failed to get link status IRQ num: %d\n",
			ret);
		return ret;
	}

	wdc->irq = ret;
	ret = devm_request_irq(wdc->dev, wdc->irq, link_status_handler,
				IRQF_ONESHOT
				| IRQF_TRIGGER_RISING
				| IRQF_TRIGGER_FALLING,
				"wdc-link-status", wdc);
	if (ret != 0) {
		wdc->irq = 0;
		dev_err(wdc->dev, "Failed to request link status IRQ: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int setup_extcon(struct wdc *wdc)
{
	struct device *dev = wdc->dev;
	int ret;

	wdc->extcon = devm_extcon_dev_allocate(dev, supported_cables);
	if (IS_ERR(wdc->extcon)) {
		ret = PTR_ERR(wdc->extcon);
		dev_err(dev, "Failed to allocate extcon: %d\n", ret);
		return ret;
	}

	ret = devm_extcon_dev_register(dev, wdc->extcon);
	if (ret < 0) {
		dev_err(dev, "Failed to register extcon: %d\n", ret);
		return ret;
	}

	return 0;
}

static void prep_ext_ctrl(struct wdc *wdc)
{
	struct wdc_ext_ctrl *ctrl = &wdc->ext_ctrl;
	if (IS_ERR(ctrl->extcon))
		return;

	ctrl->nb.notifier_call = controller_change_notify;
	INIT_WORK(&ctrl->work, controller_change_work);
}

static struct regulator *setup_vdd(struct device *dev)
{
	int ret;
	struct regulator *vdd = devm_regulator_get(dev, "google,wdc-vdd");

	if (IS_ERR(vdd)) {
		ret = PTR_ERR(vdd);
		dev_err(dev, "Unable to get VDD regulator: %d\n", ret);
		return ERR_PTR(ret);
	}

	ret = regulator_set_load(vdd, MAX_LOAD_UA);
	if (ret < 0) {
		dev_err(dev, "Unable to set VDD load: %d\n", ret);
		return ERR_PTR(ret);
	}

	ret = regulator_set_voltage(vdd, VDD_UV, VDD_UV);
	if (ret < 0) {
		dev_err(dev, "Unable to set VDD voltage: %d\n", ret);
		return ERR_PTR(ret);
	}

	return vdd;
}

static int wdc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wdc *wdc;
	int ret;
	struct regulator *vdd;

	dev_dbg(dev, "Probe start\n");

	vdd = setup_vdd(dev);
	if (IS_ERR(vdd))
		return PTR_ERR(vdd);

	wdc = devm_kzalloc(&pdev->dev, sizeof(*wdc), GFP_KERNEL);
	if (!wdc)
		return -ENOMEM;

	platform_set_drvdata(pdev, wdc);
	dev_set_drvdata(dev, wdc);
	wdc->dev = dev;
	wdc->vdd = vdd;

	mutex_init(&wdc->lock);

	wdc->ext_ctrl.extcon = extcon_get_edev_by_phandle(wdc->dev, 0);
	if (IS_ERR(wdc->ext_ctrl.extcon)) {
		ret = PTR_ERR(wdc->ext_ctrl.extcon);

		/* Only care about a valid controller that isn't ready yet. */
		if (ret == -EPROBE_DEFER)
			return ret;
	}

	INIT_DELAYED_WORK(&wdc->status_update_work, status_update_work);

	ret = of_property_read_u32(dev->of_node,
				"google,link-lost-debounce-ms",
				&wdc->link_lost_debounce_ms);
	if (ret < 0)
		wdc->link_lost_debounce_ms = DEFAULT_LINK_LOST_DEBOUNCE_MS;

	dev_info(wdc->dev, "Link lost debounce: %u ms\n",
		wdc->link_lost_debounce_ms);

	/* Create a wakeup source for use with pm_wakeup_dev_event() */
	ret = device_init_wakeup(wdc->dev, true /*enable*/);
	if (ret < 0) {
		dev_err(wdc->dev, "Could not create wakeup source: %d\n", ret);
		/* Not fatal */
		ret = 0;
	}

	ret = setup_extcon(wdc);
	if (ret < 0)
		return ret;

	ret = setup_gpios(wdc);
	if (ret < 0)
		return ret;

	/* Update state now */
	do_status_update(wdc);

	ret = setup_link_status_irq(wdc);
	if (ret < 0)
		return ret;

	prep_ext_ctrl(wdc);

	setup_sysfs(wdc);

	return 0;
}

static void wdc_remove(struct platform_device *pdev)
{
	struct wdc *wdc = platform_get_drvdata(pdev);
	teardown_sysfs(wdc);

	if (wdc->ext_ctrl.en)
		ext_ctrl_en(wdc, false);
	cancel_work_sync(&wdc->ext_ctrl.work);

	disable_irq(wdc->irq);
	cancel_delayed_work_sync(&wdc->status_update_work);

	/*
	 * Ensures regulator calls are balanced by calling
	 * regulator_disable() if needed
	 */
	enable(wdc, false);

	device_init_wakeup(wdc->dev, false /*disable*/);
	mutex_destroy(&wdc->lock);
}

static int wdc_pm_suspend(struct device *dev)
{
	struct wdc *wdc = dev_get_drvdata(dev);
	if (wdc->irq) {
		int ret = enable_irq_wake(wdc->irq);
		if (ret < 0)
			dev_warn(dev, "Could not enable IRQ wake: %d\n", ret);
	}

	return 0;
}

static int wdc_pm_resume(struct device *dev)
{
	struct wdc *wdc = dev_get_drvdata(dev);
	if (wdc->irq) {
		int ret = disable_irq_wake(wdc->irq);
		if (ret < 0)
			dev_warn(dev, "Could not disable IRQ wake: %d\n", ret);

	}

	return 0;
}

static const struct dev_pm_ops wdc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(wdc_pm_suspend, wdc_pm_resume)
};

static const struct of_device_id match_table[] = {
	{ .compatible = "google,wdc" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, match_table);
static struct platform_driver wdc_driver = {
	.driver	= {
		.name = "google-wdc",
		.of_match_table	= of_match_ptr(match_table),
		.pm = &wdc_pm_ops,
	},
	.probe	= wdc_probe,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0))
	.remove_new = wdc_remove,
#else
	.remove = wdc_remove,
#endif
};
module_platform_driver(wdc_driver);
MODULE_DESCRIPTION("Google Wireless Debug Communication Driver");
MODULE_LICENSE("GPL v2");
