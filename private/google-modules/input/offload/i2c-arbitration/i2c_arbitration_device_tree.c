// SPDX-License-Identifier: GPL-2.0
/*
 * I2C Arbitration module device tree related implementation file for input offload
 *
 * Copyright 2026 Google LLC.
 */

#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#include "i2c_arbitration_device_tree.h"
#include "i2c_arbitration_log.h"

static int i2c_arbitration_parse_panel(struct device_node *np,
				       struct i2c_arbitration_dt_data *dt_data)
{
	struct device_node *panel_node;
	struct drm_panel *panel;
	int i;
	int count;

	// Parse display panel for power state notifier
	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return -ENODATA;
	for (i = 0; i < count; i++) {
		panel_node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(panel_node);
		if (!IS_ERR(panel)) {
			dt_data->panel = panel;
			of_node_put(panel_node);
			return 0;
		}
		// Release node accessing
		of_node_put(panel_node);
	}
	return PTR_ERR(panel);
}

int i2c_arbitration_parse_dt(struct platform_device *pdev, struct i2c_arbitration_dt_data *dt_data)
{
	struct device_node *np = pdev->dev.of_node;
	int rc = 0;

	LOGI("Parsing device tree\n");

	if (!np) {
		LOGE("No Device Tree node found\n");
		rc = -EINVAL;
		goto exit_error;
	}

	rc = i2c_arbitration_parse_panel(np, dt_data);
	if (rc) {
		LOGE("Failed to parse display panel, rc=%d\n", rc);
		goto exit_error;
	}
exit_error:
	return rc;
}
