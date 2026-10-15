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

#pragma once

#include <linux/gpio/consumer.h>

#include "autobahn/signal_out.h"

/* Create and initialize an abn_SignalOut object. This must be followed
 * by a call to the Destroy method in order to release allocated resources.
 *
 * gpio_handle must be a valid gpio.
 *
 * Returns 0 on success, and sets abn_SignalOut to point to the created object.
 */
int abn_SignalOut_Create(struct abn_SignalOut **abn_SignalOut,
			 struct gpio_desc *gpio_handle);

/* Destroy an abn_SignalOut object and release its allocated resources. */
void abn_SignalOut_Destroy(struct abn_SignalOut *abn_SignalOut);
