/* SPDX-License-Identifier: GPL-2.0 */
/*
 * I2C Arbitration module device tree header file for input offload
 *
 * Copyright 2026 Google LLC.
 */
#ifndef _I2C_ARBITRATION_DEVICE_TREE_H_
#define _I2C_ARBITRATION_DEVICE_TREE_H_

#include <drm/drm_panel.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

struct i2c_arbitration_dt_data {
	struct drm_panel *panel;
};

extern int i2c_arbitration_parse_dt(struct platform_device *pdev,
				    struct i2c_arbitration_dt_data *dt_data);

#endif // _I2C_ARBITRATION_DEVICE_TREE_H_
