/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google, Inc.
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

#ifndef _AUTOBAHN_HW_RESET_H
#define _AUTOBAHN_HW_RESET_H

#include <linux/kernel.h>

#include "main.h"

void autobahn_pmic_mag_ctrl(struct autobahn_data *data, bool enable);

void autobahn_pmic_afe_tx_sup_ctrl(struct autobahn_data *data, bool enable);

void autobahn_pmic_clk32_ctrl(struct autobahn_data *data, bool enable);

ssize_t autobahn_try_hw_reset(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count);

void autobahn_hw_reset(struct autobahn_data *data, int boot_mode);

ssize_t autobahn_pin_nreset_get(struct device *dev,
				struct device_attribute *attr, char *buf);

ssize_t autobahn_pin_boot0_get(struct device *dev,
			       struct device_attribute *attr, char *buf);

ssize_t autobahn_pin_boot2_get(struct device *dev,
			       struct device_attribute *attr, char *buf);

ssize_t autobahn_pin_nreset_set(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);

ssize_t autobahn_pin_boot0_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count);

ssize_t autobahn_pin_boot2_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count);

ssize_t autobahn_pin_blstatus_get(struct device *dev,
				  struct device_attribute *attr, char *buf);

#endif /* _AUTOBAHN_HW_RESET_H */
