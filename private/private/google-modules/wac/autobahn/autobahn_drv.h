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

#ifndef _AUTOBAHN_DRV_H
#define _AUTOBAHN_DRV_H

#include <linux/gpio/consumer.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "autobahn/transport.h"

/* TO DO: update the name as soon as we get rid of nanohub. */
#define AUTOBAHN_NAME "nanohub"

int autobahn_drv_core_init(struct device *dev,
			   struct abn_Transport *abn_transport,
			   struct gpio_desc *signal_in,
			   struct gpio_desc *signal_out);
void autobahn_drv_core_deinit(void);

int autobahn_drv_client_interfaces_init(void *pdata);
void autobahn_drv_client_interfaces_deinit(void);

int autobahn_drv_init(void);
void autobahn_drv_exit(void);

void autobahn_drv_start_abn_thread(void);

int autobahn_drv_pause(void);
void autobahn_drv_unpause(void);

int autobahn_drv_suspend(void);
int autobahn_drv_suspend_noirq(void);
int autobahn_drv_resume(void);

#endif /* _AUTOBAHN_DRV_H */
