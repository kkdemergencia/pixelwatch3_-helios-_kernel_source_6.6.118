/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2023 Google LLC */

#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <dt-bindings/extcon/google,extcon-shim.h>

#define NUM_SUPPORTED_CABLES 2

struct conn_extcon_mapping {
	int conn_type;
	int id;
};

static const struct conn_extcon_mapping conn_extcon_map[] = {
	{
		.conn_type=CONN_USB_2_0,
		.id=EXTCON_USB
	},
	{
		.conn_type=CONN_CHG,
		.id=EXTCON_CHG_USB_FAST
	},
	{
		.conn_type=CONN_DOCK,
		.id=EXTCON_DOCK
	},
};

struct extcon_shim {
	struct mutex lock;
	struct device *dev;
	struct extcon_dev *extcon;
	const struct conn_extcon_mapping *conn_extcon_mapping;

	int supported_cables[NUM_SUPPORTED_CABLES];

	/* The "supplier" is the extcon device we receive state updates from.
	 * i.e. the "real" extcon device.
	 */
	struct extcon_dev *supplier;
	struct notifier_block supplier_notifier;
	const struct conn_extcon_mapping *supplier_conn_extcon_mapping;
	struct work_struct supplier_update_work;
	bool supplier_connected;

	/* Always indicate no connection present */
	bool conn_force_disable;

	bool spoof_connected;
};

static void update(struct extcon_shim *shim, bool *state, bool newval)
{
	bool report_connected;
	int ret;

	mutex_lock(&shim->lock);
	if (state)
		*state = newval;

	report_connected = shim->supplier_connected
			&& !shim->conn_force_disable;
	report_connected |= !!shim->spoof_connected;

	dev_info(shim->dev, "Report %s\n",
		report_connected ? "connected" : "disconnected");

	ret = extcon_set_state_sync(shim->extcon, shim->conn_extcon_mapping->id,
				report_connected);

	if (ret != 0)
		dev_err(shim->dev, "Could not set extcon state: %d\n", ret);

	mutex_unlock(&shim->lock);
}

static void supplier_update_work(struct work_struct *work)
{
	struct extcon_shim *shim = container_of(work, struct extcon_shim,
						supplier_update_work);

	bool supplier_connected =
		(extcon_get_state(shim->supplier,
				shim->supplier_conn_extcon_mapping->id) != 0);

	update(shim, &shim->supplier_connected, supplier_connected);
}

static int supplier_change(struct notifier_block *notifier,
			unsigned long supplier_connected, void *unused)
{
	struct extcon_shim *shim = container_of(notifier,
						struct extcon_shim,
						supplier_notifier);
	schedule_work(&shim->supplier_update_work);
	return NOTIFY_DONE;
}

static ssize_t force_disable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct extcon_shim *shim = dev_get_drvdata(dev);
	bool force_disable;
	int ret;

	ret = kstrtobool(buf, &force_disable);
	if (ret < 0) {
		return ret;
	}

	update(shim, &shim->conn_force_disable, force_disable);

	return count;
}

static ssize_t force_disable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct extcon_shim *shim = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", shim->conn_force_disable);
}

static const DEVICE_ATTR_RW(force_disable);

static ssize_t spoof_connected_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct extcon_shim *shim = dev_get_drvdata(dev);
	bool spoof_connected;
	int ret;

	ret = kstrtobool(buf, &spoof_connected);
	if (ret < 0) {
		return ret;
	}

	update(shim, &shim->spoof_connected, spoof_connected);

	return count;
}

static ssize_t spoof_connected_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct extcon_shim *shim = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", shim->spoof_connected);
}

static const DEVICE_ATTR_RW(spoof_connected);

static const struct conn_extcon_mapping *get_mapping(int conn_type)
{
	int x;
	for (x = 0; x < ARRAY_SIZE(conn_extcon_map); x++) {
		if(conn_extcon_map[x].conn_type == conn_type)
			return &conn_extcon_map[x];
	}

	return NULL;
}

static int find_supplier(struct extcon_shim *shim)
{
	struct device *dev = shim->dev;
	struct device_node *node = dev->of_node;
	struct extcon_dev *supplier;
	int num_extcon;
	unsigned int supplier_type;
	const struct conn_extcon_mapping *mapping;
	int ret;

	num_extcon = of_count_phandle_with_args(node, "extcon", NULL);
	if (num_extcon < 0) {
		dev_err(dev, "Extcon count failed\n");
		return -ENODEV;
	}

	if (num_extcon != 1) {
		dev_err(dev, "Only one extcon allowed, %d provided\n",
			num_extcon);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "google,supplier-connection-type",
				&supplier_type);
	if (ret < 0) {
		dev_err(dev, "Must provide a supplier connection type\n");
		return ret;
	}

	mapping = get_mapping(supplier_type);
	if (!mapping) {
		dev_err(dev, "Invalid supplier connection type %u\n",
			supplier_type);
		return -EINVAL;
	}

	supplier = extcon_get_edev_by_phandle(dev, 0);
	if (IS_ERR(supplier)) {
		ret = PTR_ERR(supplier);
		if (ret != -EPROBE_DEFER) {
			dev_err(dev, "Could not get supplier extcon: %d\n",
				ret);
		}
		return ret;
	}

	shim->supplier = supplier;
	shim->supplier_conn_extcon_mapping = mapping;

	return 0;
}

static int supplier_notification_setup(struct extcon_shim *shim)
{
	int ret;
	INIT_WORK(&shim->supplier_update_work, supplier_update_work);

	shim->supplier_notifier.notifier_call = supplier_change;
	ret = devm_extcon_register_notifier(shim->dev, shim->supplier,
					shim->supplier_conn_extcon_mapping->id,
					&shim->supplier_notifier);
	if (ret < 0)
		dev_err(shim->dev,
			"Failed to register notifier for supplier: %d\n", ret);

	return ret;
}

static int setup_own_extcon(struct extcon_shim *shim)
{
	struct device *dev = shim->dev;
	struct device_node *node = dev->of_node;
	unsigned int conn_type;
	const struct conn_extcon_mapping *mapping;
	int ret = 0;

	ret = of_property_read_u32(node, "google,connection-type", &conn_type);
	if (ret < 0) {
		dev_err(dev, "Must provide a connection type\n");
		return ret;
	}

	mapping = get_mapping(conn_type);
	if (!mapping) {
		dev_err(dev, "Invalid connection type %u\n",
			conn_type);
		return -EINVAL;
	}

	shim->conn_extcon_mapping = mapping;

	shim->supported_cables[0] = shim->conn_extcon_mapping->id;
	/* Last value is sentinel */
	shim->supported_cables[NUM_SUPPORTED_CABLES - 1] = EXTCON_NONE;

	shim->extcon = devm_extcon_dev_allocate(dev, shim->supported_cables);
	if (IS_ERR(shim->extcon)) {
		ret = PTR_ERR(shim->extcon);
		dev_err(dev, "Failed to allocate extcon device: %d\n", ret);
		return ret;
	}

	ret = devm_extcon_dev_register(dev, shim->extcon);
	if (ret < 0) {
		dev_err(dev, "Failed to register extcon device: %d\n", ret);
		return ret;
	}

	return ret;
}

static int setup_sysfs(struct extcon_shim *shim)
{
	struct device *dev = shim->dev;
	int ret;

	ret = device_create_file(shim->dev, &dev_attr_force_disable);
	if (ret < 0) {
		dev_err(dev, "Failed to create %s: %d\n",
			dev_attr_force_disable.attr.name, ret);

		/* Nonfatal */
		ret = 0;
	}

	ret = device_create_file(shim->dev, &dev_attr_spoof_connected);
	if (ret < 0) {
		dev_err(dev, "Failed to create %s: %d\n",
			dev_attr_spoof_connected.attr.name, ret);

		/* Nonfatal */
		ret = 0;
	}

	return ret;
}

static void teardown_sysfs(struct extcon_shim *shim)
{
	device_remove_file(shim->dev, &dev_attr_force_disable);
	device_remove_file(shim->dev, &dev_attr_spoof_connected);
}

static void setup_force_disabled_at_init(struct extcon_shim *shim)
{
	struct device *dev = shim->dev;
	struct device_node *node = dev->of_node;
	bool start_force_disabled;

	start_force_disabled =
		of_property_read_bool(node, "google,force-disabled-at-init");

	shim->conn_force_disable = start_force_disabled;

	dev_info(dev, "Shim start force disabled:%u\n", start_force_disabled);
}

static int extcon_shim_probe(struct platform_device *pdev)
{
	struct extcon_shim *shim;
	int ret = 0;

	shim = devm_kzalloc(&pdev->dev, sizeof(*shim), GFP_KERNEL);
	if (!shim)
		return -ENOMEM;

	platform_set_drvdata(pdev, shim);
	dev_set_drvdata(&pdev->dev, shim);
	shim->dev = &pdev->dev;

	mutex_init(&shim->lock);

	ret = find_supplier(shim);
	if (ret < 0)
		return ret;

	ret = setup_own_extcon(shim);
	if (ret < 0) {
		dev_err(shim->dev, "Own extcon setup failed: %d\n", ret);
		return ret;
	}

	setup_force_disabled_at_init(shim);

	ret = supplier_notification_setup(shim);
	if (ret < 0) {
		/*
		 * Spoof the connection so that this path turns on to facilitate
		 * debug.
		 */
		dev_err(shim->dev,
			"Supplier notification setup failed: %d\n", ret);
		dev_err(shim->dev, "Extcon will report as connected\n");
		shim->spoof_connected = true;
	}

	ret = setup_sysfs(shim);
	if (ret < 0) {
		dev_err(shim->dev, "Failed to set up sysfs: %d\n", ret);
		dev_err(shim->dev, "Extcon will report as connected\n");
		shim->spoof_connected = true;
	}

	schedule_work(&shim->supplier_update_work);

	return 0;
}

static void extcon_shim_remove(struct platform_device *pdev)
{
	struct extcon_shim *shim = platform_get_drvdata(pdev);

	teardown_sysfs(shim);
	mutex_destroy(&shim->lock);
	dev_set_drvdata(&pdev->dev, NULL);
	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id match_table[] = {
	{ .compatible = "google,extcon-shim" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, match_table);

static struct platform_driver extcon_shim = {
	.driver	= {
		.name = "extcon-shim",
		.of_match_table	= of_match_ptr(match_table),
		.pm = NULL,
	},
	.probe		= extcon_shim_probe,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0))
	.remove_new	= extcon_shim_remove,
#else
	.remove		= extcon_shim_remove,
#endif
};
module_platform_driver(extcon_shim);

MODULE_DESCRIPTION("Extcon shim");
MODULE_LICENSE("GPL v2");
