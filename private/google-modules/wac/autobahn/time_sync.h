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


#ifndef __AUTOBAHN_TIME_SYNC_H
#define __AUTOBAHN_TIME_SYNC_H

#include <linux/device.h>
#include <linux/types.h>

struct time_sync_data;

ssize_t time_sync_show(struct device *dev,
		       struct device_attribute *attr, char *buf);

int time_sync_initialize(struct device *dev);

#endif /* __AUTOBAHN_TIME_SYNC_H */
