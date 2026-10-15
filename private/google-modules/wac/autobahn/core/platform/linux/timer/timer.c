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

#include <asm/barrier.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/types.h>

#include "autobahn/log/log.h"
#include "autobahn/timer.h"

struct abn_Timer_Handle {
	struct timer_list timer;
	abn_Timer_ExpiryHandler expiry_handler_func;
	void *handler_context;
};

static void abn_Timer_SetExpiryHandlerFunc(void *ptimer_handle,
				abn_Timer_ExpiryHandler expiry_handler,
				void *phandler_context);
static void abn_Timer_InvokeAfterFunc(void *ptimer_handle,
				      abn_Timer_Ticks delay);
static void abn_Timer_CancelFunc(void *ptimer_handle);

static const struct abn_Timer_Functions local_functions = {
	abn_Timer_SetExpiryHandlerFunc,
	abn_Timer_InvokeAfterFunc,
	abn_Timer_CancelFunc
};

static void abn_Timer_Callback(struct timer_list *data)
{
	struct abn_Timer_Handle *ptimer_handle =
			container_of(data, struct abn_Timer_Handle, timer);

	ptimer_handle->expiry_handler_func(ptimer_handle->handler_context);
}

static void abn_Timer_SetExpiryHandlerFunc(void *ptimer_handle,
				abn_Timer_ExpiryHandler expiry_handler,
				void *phandler_context)
{
	struct abn_Timer_Handle *phandle = ptimer_handle;

	WARN_ON((timer_pending(&phandle->timer) != 0));

	phandle->expiry_handler_func = expiry_handler;
	phandle->handler_context = phandler_context;

	/* Third argument is set to 0 as no special flags are being used. */
	timer_setup(&phandle->timer, abn_Timer_Callback, 0U);
}

static void abn_Timer_InvokeAfterFunc(void *ptimer_handle,
				      abn_Timer_Ticks delay)
{
	struct abn_Timer_Handle *phandle;

	phandle = ptimer_handle;
	if (phandle->expiry_handler_func == NULL) {
		ABN_LOG_WARN("Timer handler not set!");
		return;
	}

	if (timer_pending(&phandle->timer) != 0) {
		/* A timer timeout can be updated on the fly, but we leave a
		 * warning as the specifications do not require to support this
		 * case.
		 */
		ABN_LOG_WARN("Timer already scheduled for handler %p\n",
			     ptimer_handle);
	}

	mod_timer(&phandle->timer, jiffies + delay);
}

static void abn_Timer_CancelFunc(void *ptimer_handle)
{
	struct abn_Timer_Handle *phandle = ptimer_handle;

	del_timer_sync(&phandle->timer);
}

/* Allocates an abn_Timer structure. */
int abn_Timer_Create(struct abn_Timer **ppabn_timer)
{
	struct abn_Timer *pabn_timer;

	if (ppabn_timer == NULL)
		return -EINVAL;

	pabn_timer = kmalloc(sizeof(struct abn_Timer), GFP_KERNEL);
	if (pabn_timer == NULL)
		return -ENOMEM;

	pabn_timer->timer_handle = kzalloc(sizeof(struct abn_Timer_Handle),
					   GFP_KERNEL);
	if (pabn_timer->timer_handle == NULL) {
		kfree(pabn_timer);
		return -ENOMEM;
	}
	pabn_timer->functions = &local_functions;
	*ppabn_timer = pabn_timer;

	return 0;
}

/* Deallocates an abn_Timer structure previously allocated with
 * "abn_Timer_Create".
 * Assumes it is called when the client of the timer has been closed.
 */
void abn_Timer_Destroy(struct abn_Timer *pabn_timer)
{
	struct abn_Timer_Handle *phandle = pabn_timer->timer_handle;

	del_timer_sync(&phandle->timer);

	kfree(phandle);
	kfree(pabn_timer);
}

