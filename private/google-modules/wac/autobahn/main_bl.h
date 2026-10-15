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

#ifndef _AUTOBAHN_MAIN_BL_H
#define _AUTOBAHN_MAIN_BL_H

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>

#include "bl_nxp.h"

#define FW_NAME_SIZE 64

struct firmware_request {
	char fw_name[FW_NAME_SIZE];
	uint32_t fw_image_offset;
	atomic_t state;
	int result;
	struct completion fw_complete;
};

ssize_t
autobahn_firmware_name_query(struct device *dev,
			     struct device_attribute *attr,
			     char *buf);

ssize_t
autobahn_download_firmware_request(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count);

void autobahn_download_firmware(struct device *dev);


#endif
