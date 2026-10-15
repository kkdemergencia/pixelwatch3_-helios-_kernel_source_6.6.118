// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/signal.h>
#include "autobahn/signal.h"
#include "autobahn/signal_out.h"
#include "autobahn/log/log.h"

#define SIGNAL_OUT_GPIO_LABEL "autobahn_signal_out"

/* Active high signalling */

#define SIGNAL_OUT_ACTIVE_STATE 1
#define SIGNAL_OUT_INACTIVE_STATE 0

struct abn_SignalOut_Priv {
	struct gpio_desc *gpio_handle;
};

static void abn_SignalOut_SetFunc(void *signal_out_handle,
		enum abn_Signal_State state);

static const struct abn_SignalOut_Functions abn_signal_out_functions = {
	abn_SignalOut_SetFunc
};

static int abn_signal_to_gpio_signal_state(enum abn_Signal_State abn_state)
{
	switch (abn_state) {
	case ABN_SIGNAL_STATE_ACTIVE:
		return SIGNAL_OUT_ACTIVE_STATE;
	case ABN_SIGNAL_STATE_INACTIVE:
		return SIGNAL_OUT_INACTIVE_STATE;
	default:
		ABN_LOG_FATAL("Unexpected signal state: %d", abn_state);
	};
}

static void abn_SignalOut_SetFunc(void *signal_out_handle,
				  enum abn_Signal_State state)
{
	struct abn_SignalOut_Priv *priv =
		(struct abn_SignalOut_Priv *)signal_out_handle;

	gpiod_set_value(priv->gpio_handle, abn_signal_to_gpio_signal_state(state));
}

int abn_SignalOut_Create(struct abn_SignalOut **abn_SignalOut,
			 struct gpio_desc *gpio_handle)
{
	struct abn_SignalOut *mabn_SignalOut;
	struct abn_SignalOut_Priv *priv;

	if (abn_SignalOut == NULL)
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	mabn_SignalOut = kzalloc(sizeof(**abn_SignalOut), GFP_KERNEL);
	if (mabn_SignalOut == NULL) {
		kfree(priv);
		return -ENOMEM;
	}

	priv->gpio_handle = gpio_handle;

	mabn_SignalOut->signal_out_handle = priv;
	mabn_SignalOut->functions = &abn_signal_out_functions;

	*abn_SignalOut = mabn_SignalOut;

	return 0;
}

void abn_SignalOut_Destroy(struct abn_SignalOut *abn_SignalOut)
{
	if (abn_SignalOut == NULL)
		return;

	kfree(abn_SignalOut->signal_out_handle);
	kfree(abn_SignalOut);
}
