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

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

#include "autobahn/timer.h"
#include "autobahn/platform/linux/timer.h"

#define TIMEOUT_IN_MS          (500U)
#define CHECK_DELAY_IN_JIFFIES (3UL)

struct context_arg {
	atomic_t done_count;
	struct kunit *ptest;
};

static struct context_arg context;
static struct abn_Timer *pabn_timer;

static void abn_Timer_ExpiryHandlerFunc(void *phandler_context)
{
	struct context_arg *plocal_context = phandler_context;

	KUNIT_EXPECT_TRUE(plocal_context->ptest, plocal_context == &context);
	atomic_inc(&plocal_context->done_count);
}

static int abn_timer_interf_init(struct kunit *test)
{
	pabn_timer = NULL;
	atomic_set(&context.done_count, 0);
	context.ptest = NULL;
	return 0;
}

/* Test cases. */

static void abn_timer_interf_test_creation(struct kunit *test)
{
	abn_Timer_Create(&pabn_timer);
	KUNIT_EXPECT_TRUE(test, pabn_timer != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->timer_handle != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->set_expiry_handler !=
				NULL);

	abn_Timer_Destroy(pabn_timer);
}

static void abn_timer_interf_test_cancel_unarmed(struct kunit *test)
{
	abn_Timer_Create(&pabn_timer);
	KUNIT_EXPECT_TRUE(test, pabn_timer != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->timer_handle != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->set_expiry_handler !=
				NULL);

	context.ptest = test;
	pabn_timer->functions->set_expiry_handler(pabn_timer->timer_handle,
						  abn_Timer_ExpiryHandlerFunc,
						  &context);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->invoke_after != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->cancel != NULL);

	/* Check that it is safe to call cancel interface when the timer
	 * is unarmed.
	 */
	pabn_timer->functions->cancel(pabn_timer->timer_handle);

	abn_Timer_Destroy(pabn_timer);
}

static void abn_timer_interf_test_destroy_unarmed(struct kunit *test)
{
	abn_Timer_Create(&pabn_timer);
	KUNIT_EXPECT_TRUE(test, pabn_timer != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->timer_handle != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->set_expiry_handler !=
				NULL);

	context.ptest = test;
	pabn_timer->functions->set_expiry_handler(pabn_timer->timer_handle,
						  abn_Timer_ExpiryHandlerFunc,
						  &context);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->invoke_after != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->cancel != NULL);

	/* Check that the timer can be destroyed when unarmed. */
	abn_Timer_Destroy(pabn_timer);
}

static void abn_timer_interf_test_destroy_armed(struct kunit *test)
{
	int local_done_count = atomic_read(&context.done_count);

	abn_Timer_Create(&pabn_timer);
	KUNIT_EXPECT_TRUE(test, pabn_timer != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->timer_handle != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->set_expiry_handler !=
				NULL);

	context.ptest = test;
	pabn_timer->functions->set_expiry_handler(pabn_timer->timer_handle,
						  abn_Timer_ExpiryHandlerFunc,
						  &context);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->invoke_after != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->cancel != NULL);

	/* Check that the timer can be destroyed when armed. */
	pabn_timer->functions->invoke_after(pabn_timer->timer_handle,
					    msecs_to_jiffies(TIMEOUT_IN_MS));
	abn_Timer_Destroy(pabn_timer);
	msleep(TIMEOUT_IN_MS + jiffies_to_msecs(CHECK_DELAY_IN_JIFFIES));
	KUNIT_EXPECT_TRUE(test, atomic_read(&context.done_count) ==
				local_done_count);
}

static void abn_timer_interf_test_triggered(struct kunit *test)
{
	int i;
	int local_done_count = atomic_read(&context.done_count);

	abn_Timer_Create(&pabn_timer);
	KUNIT_EXPECT_TRUE(test, pabn_timer != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->timer_handle != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->set_expiry_handler !=
				NULL);

	context.ptest = test;
	pabn_timer->functions->set_expiry_handler(pabn_timer->timer_handle,
						  abn_Timer_ExpiryHandlerFunc,
						  &context);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->invoke_after != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->cancel != NULL);

	/* Arm the timer for a few times and check the callback is called. */
	for (i = 0; i < 3; i++) {
		pabn_timer->functions->invoke_after(pabn_timer->timer_handle,
						msecs_to_jiffies(TIMEOUT_IN_MS));
		msleep(TIMEOUT_IN_MS + jiffies_to_msecs(CHECK_DELAY_IN_JIFFIES));
		KUNIT_EXPECT_TRUE(test, atomic_read(&context.done_count) ==
					++local_done_count);
	}

	abn_Timer_Destroy(pabn_timer);
}

static void abn_timer_interf_test_cancelled(struct kunit *test)
{
	int i;
	int local_done_count = atomic_read(&context.done_count);

	abn_Timer_Create(&pabn_timer);
	KUNIT_EXPECT_TRUE(test, pabn_timer != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->timer_handle != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->set_expiry_handler !=
				NULL);

	context.ptest = test;
	pabn_timer->functions->set_expiry_handler(pabn_timer->timer_handle,
						  abn_Timer_ExpiryHandlerFunc,
						  &context);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->invoke_after != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_timer->functions->cancel != NULL);

	/* Arm and cancel the timer, then check that the timer callback is not
	 * called. Repeat these steps for a few times.
	 */
	for (i = 0; i < 3; i++) {
		pabn_timer->functions->invoke_after(pabn_timer->timer_handle,
					msecs_to_jiffies(TIMEOUT_IN_MS));
		msleep(TIMEOUT_IN_MS / 2);
		pabn_timer->functions->cancel(pabn_timer->timer_handle);
		msleep(TIMEOUT_IN_MS / 2 +
		       jiffies_to_msecs(CHECK_DELAY_IN_JIFFIES));
		KUNIT_EXPECT_TRUE(test, atomic_read(&context.done_count) ==
					local_done_count);
	}

	abn_Timer_Destroy(pabn_timer);
}

static struct kunit_case abn_timer_interf_test_cases[] = {
	KUNIT_CASE(abn_timer_interf_test_creation),
	KUNIT_CASE(abn_timer_interf_test_cancel_unarmed),
	KUNIT_CASE(abn_timer_interf_test_destroy_unarmed),
	KUNIT_CASE(abn_timer_interf_test_destroy_armed),
	KUNIT_CASE(abn_timer_interf_test_triggered),
	KUNIT_CASE(abn_timer_interf_test_cancelled),
	{}
};

static struct kunit_suite abn_timer_interf_test_suite = {
	.name = "abn_timer_interf_test_suite",
	.init = abn_timer_interf_init,
	.test_cases = abn_timer_interf_test_cases
};

kunit_test_suite(abn_timer_interf_test_suite);

MODULE_AUTHOR("Florin Curelaru");
MODULE_LICENSE("GPL");
