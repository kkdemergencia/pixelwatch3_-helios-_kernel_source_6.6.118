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

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/types.h>

#include "autobahn/log/log.h"
#include "autobahn/platform/linux/kunit.h"
#include "autobahn/signal.h"
#include "autobahn/signal_in.h"

#define SIGNAL_IN_ACTIVE_STATE 1
#define SIGNAL_IN_INACTIVE_STATE 0

/* Private abn_SignalIn data, stored in signal_in_handle. */

struct abn_SignalIn_Priv {
	abn_SignalIn_TransitionHandler interrupt_handler;
	void *handler_context;
	struct gpio_desc *gpio_handle;
	unsigned int irq;
	bool irq_enabled;
};

static enum abn_Signal_State abn_SignalIn_GetFunc(void *signal_in_handle);

static void
abn_SignalIn_SetTransitionHandlerFunc(void *signal_in_handle,
				     abn_SignalIn_TransitionHandler handler,
				     void *handler_context);

static void abn_SignalIn_EnableTransitionHandlerFunc(void *signal_in_handle,
						    bool enable);

const struct abn_SignalIn_Functions abn_signal_in_functions = {
	abn_SignalIn_GetFunc,
	abn_SignalIn_SetTransitionHandlerFunc,
	abn_SignalIn_EnableTransitionHandlerFunc
};

void __mockable abn_SignalIn_enable_irq_wrapper(int irq)
{
	enable_irq(irq);
}

void __mockable abn_SignalIn_disable_irq_wrapper(int irq)
{
	disable_irq(irq);
}

int __mockable abn_SignalIn_gpiod_to_irq_wrapper(struct gpio_desc *gpio)
{
	return gpiod_to_irq(gpio);
}

int __mockable
abn_SignalIn_request_irq_wrapper(int irq, irq_handler_t handler,
				 unsigned long flags,
				 const char *name, void *dev)
{
	return request_irq(irq, handler, flags, name, dev);
}

void __mockable abn_SignalIn_free_irq_wrapper(int irq, void *dev)
{
	free_irq(irq, dev);
}

static enum abn_Signal_State gpio_signal_to_abn_signal_state(int gpio_state)
{
	switch (gpio_state) {
	case SIGNAL_IN_INACTIVE_STATE:
		return ABN_SIGNAL_STATE_INACTIVE;
	case SIGNAL_IN_ACTIVE_STATE:
		return ABN_SIGNAL_STATE_ACTIVE;
	default:
		ABN_LOG_FATAL("Unexpected signal state: %d", gpio_state);
	};
}

static irqreturn_t handle_irq(int irq, void *dev)
{
	struct abn_SignalIn_Priv *priv = (struct abn_SignalIn_Priv *)dev;

	if (priv->interrupt_handler != NULL)
		priv->interrupt_handler(priv->handler_context);

	return IRQ_HANDLED;
}

static enum abn_Signal_State abn_SignalIn_GetFunc(void *signal_in_handle)
{
	int value;
	struct abn_SignalIn_Priv *priv =
		(struct abn_SignalIn_Priv *)signal_in_handle;

	value = gpiod_get_value(priv->gpio_handle);
	return gpio_signal_to_abn_signal_state(value);
}

static void
abn_SignalIn_SetTransitionHandlerFunc(void *signal_in_handle,
				     abn_SignalIn_TransitionHandler handler,
				     void *handler_context)
{
	struct abn_SignalIn_Priv *priv =
		(struct abn_SignalIn_Priv *)signal_in_handle;

	/* Precondition for setting the handler is that interrupts are disabled */

	if (priv->irq_enabled) {
		ABN_LOG_ERROR("cannot set interrupt handler while interrupts are enabled");
		return;
	}

	priv->interrupt_handler = handler;
	priv->handler_context = handler_context;

	/* Ensure writes are done before the interrupt is enabled. */
	smp_wmb();
}

static void abn_SignalIn_EnableTransitionHandlerFunc(void *signal_in_handle,
						    bool enable)
{
	struct abn_SignalIn_Priv *priv =
		(struct abn_SignalIn_Priv *)signal_in_handle;

	/* We are keeping track of the irq state since enable_irq and
	 * disable_irq are nested calls. We want to avoid enabling/disabling
	 * them multiple times in a row.
	 */

	if (enable == priv->irq_enabled)
		return;

	/* Precondition for enabling the irq is that the interrupt handler is set. */

	if (enable && priv->interrupt_handler != NULL) {
		abn_SignalIn_enable_irq_wrapper(priv->irq);
		priv->irq_enabled = true;
	} else {
		abn_SignalIn_disable_irq_wrapper(priv->irq);
		priv->irq_enabled = false;
	}
}

int abn_SignalIn_Create(struct abn_SignalIn **abn_SignalIn,
			struct gpio_desc *gpio_handle)
{
	struct abn_SignalIn *mabn_SignalIn;
	struct abn_SignalIn_Priv *priv;
	int ret = 0;
	int irq;

	if (abn_SignalIn == NULL)
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	mabn_SignalIn = kzalloc(sizeof(**abn_SignalIn), GFP_KERNEL);
	if (mabn_SignalIn == NULL) {
		kfree(priv);
		return -ENOMEM;
	}

	irq = abn_SignalIn_gpiod_to_irq_wrapper(gpio_handle);
	if (irq < 0) {
		ABN_LOG_ERROR("Failed to request irq! err=%d\n", irq);
		kfree(priv);
		kfree(mabn_SignalIn);
		return irq;
	}

	/* Call request_irq using IRQF_NO_AUTOEN flag so as not to enable the irq
	 * automatically.
	 */

	ret = abn_SignalIn_request_irq_wrapper(irq, handle_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_NO_AUTOEN,
			"autobahn-irq", priv);

	if (ret < 0) {
		ABN_LOG_ERROR("request_irq failure: %d", ret);
		kfree(priv);
		kfree(mabn_SignalIn);
		return ret;
	}

	priv->gpio_handle = gpio_handle;
	priv->irq = irq;
	priv->irq_enabled = false;
	priv->interrupt_handler = NULL;
	mabn_SignalIn->signal_in_handle = priv;
	mabn_SignalIn->functions = &abn_signal_in_functions;

	*abn_SignalIn = mabn_SignalIn;

	return ret;
}

void abn_SignalIn_Destroy(struct abn_SignalIn *abn_SignalIn)
{
	struct abn_SignalIn_Priv *priv;

	if (abn_SignalIn == NULL)
		return;

	priv = abn_SignalIn->signal_in_handle;

	if (priv != NULL)
		abn_SignalIn_free_irq_wrapper(priv->irq, priv);

	kfree(priv);
	kfree(abn_SignalIn);
}
