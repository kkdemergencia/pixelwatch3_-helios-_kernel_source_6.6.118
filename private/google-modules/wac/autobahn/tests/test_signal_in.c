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
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>

#include "autobahn/platform/linux/signal_in.h"
#include "autobahn/signal.h"
#include "autobahn/signal_in.h"

#define GPIO_HANDLE ((struct gpio_desc *) 0xdeadbeef)
#define FAKE_HANDLER_CONTEXT_PTR ((void *)0xcafef0ca)

/* Mocking variable. */
static bool irq_enabled;

/* Mock irq functions. Works because wrappers are __weak symbols. */
void abn_SignalIn_enable_irq_wrapper(int irq)
{
	irq_enabled = true;
}

void abn_SignalIn_disable_irq_wrapper(int irq)
{
	irq_enabled = false;
}

int abn_SignalIn_gpiod_to_irq_wrapper(struct gpio_desc *gpio)
{
	int irq = 10;
	return irq;
}

int abn_SignalIn_request_irq_wrapper(int irq, irq_handler_t handler,
				     unsigned long flags,
				     const char *name, void *dev)
{
	return 0;
}

void abn_SignalIn_free_irq_wrapper(int irq, void *dev) {}

static void interrupt_handler(void *handler_context) {}

static void create_destroy(struct kunit *test)
{
	struct abn_SignalIn *abn_SignalIn = NULL;

	KUNIT_EXPECT_EQ(test, 0, abn_SignalIn_Create(&abn_SignalIn, GPIO_HANDLE));

	KUNIT_EXPECT_TRUE(test, abn_SignalIn != NULL);
	KUNIT_EXPECT_TRUE(test, abn_SignalIn->signal_in_handle != NULL);
	KUNIT_EXPECT_TRUE(test, abn_SignalIn->functions->get != NULL);
	KUNIT_EXPECT_TRUE(test, abn_SignalIn->functions->set_interrupt_handler != NULL);
	KUNIT_EXPECT_TRUE(test, abn_SignalIn->functions->enable_interrupt_handler != NULL);

	abn_SignalIn_Destroy(abn_SignalIn);
}

struct kunit_case autobahn_signal_in_create_test_cases[] = {
	KUNIT_CASE(create_destroy),
	{}
};

struct kunit_suite autobahn_signal_in_create_test_suite = {
	.name = "autobahn_signal_in_create_test_suite",
	.test_cases = autobahn_signal_in_create_test_cases
};

static int autobahn_signal_in_init(struct kunit *test)
{
	int ret;
	struct abn_SignalIn *abn_SignalIn = NULL;

	ret = abn_SignalIn_Create(&abn_SignalIn, GPIO_HANDLE);
	if (ret < 0)
		return ret;

	irq_enabled = false;

	test->priv = abn_SignalIn;

	return 0;
}

static void autobahn_signal_in_exit(struct kunit *test)
{
	abn_SignalIn_Destroy((struct abn_SignalIn *)test->priv);
}

static void enable_irq_null_handler(struct kunit *test)
{
	struct abn_SignalIn *abn_SignalIn = (struct abn_SignalIn *)test->priv;

	/* Set irq handler to NULL */
	abn_SignalIn->functions->set_interrupt_handler(
		abn_SignalIn->signal_in_handle, NULL,
		FAKE_HANDLER_CONTEXT_PTR);

	/* Cannot enable irq if irq handler is NULL */
	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, true);
	KUNIT_EXPECT_EQ(test, false, irq_enabled);
}

static void enable_disable_irq(struct kunit *test)
{
	struct abn_SignalIn *abn_SignalIn = (struct abn_SignalIn *)test->priv;

	/* Check that irq is disabled */
	KUNIT_EXPECT_EQ(test, false, irq_enabled);

	/* Set irq handler */
	abn_SignalIn->functions->set_interrupt_handler(
		abn_SignalIn->signal_in_handle, interrupt_handler,
		FAKE_HANDLER_CONTEXT_PTR);

	/* Enable irq */
	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, true);
	KUNIT_EXPECT_EQ(test, true, irq_enabled);

	/* Disable irq */
	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, false);
	KUNIT_EXPECT_EQ(test, false, irq_enabled);
}

static void set_irq_handler(struct kunit *test)
{
	struct abn_SignalIn *abn_SignalIn = (struct abn_SignalIn *)test->priv;

	/* Set irq handler */
	abn_SignalIn->functions->set_interrupt_handler(
		abn_SignalIn->signal_in_handle, interrupt_handler,
		FAKE_HANDLER_CONTEXT_PTR);

	/* Enable irq */
	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, true);
	KUNIT_EXPECT_EQ(test, true, irq_enabled);

	/* Attempt to set irq handler to NULL while interrupts are enabled */
	abn_SignalIn->functions->set_interrupt_handler(
		abn_SignalIn->signal_in_handle, NULL,
		FAKE_HANDLER_CONTEXT_PTR);

	/* Disable irq */
	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, false);
	KUNIT_EXPECT_EQ(test, false, irq_enabled);

	/* Enable irq, this should be possible because the set irq handler to NULL is
	 * not allowed when irq is enabled.
	 */

	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, true);
	KUNIT_EXPECT_EQ(test, true, irq_enabled);

	/* Disable irq */
	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, false);
	KUNIT_EXPECT_EQ(test, false, irq_enabled);

	/* Set irq handler to NULL while interrupts are disabled */
	abn_SignalIn->functions->set_interrupt_handler(
		abn_SignalIn->signal_in_handle, NULL,
		FAKE_HANDLER_CONTEXT_PTR);

	/* Attempt to enable irq. This should not be possible because the handler was
	 * successfully set to NULL.
	 */

	abn_SignalIn->functions->enable_interrupt_handler(
		abn_SignalIn->signal_in_handle, true);
	KUNIT_EXPECT_EQ(test, false, irq_enabled);
}

struct kunit_case autobahn_signal_in_irq_test_cases[] = {
	KUNIT_CASE(enable_irq_null_handler),
	KUNIT_CASE(enable_disable_irq),
	KUNIT_CASE(set_irq_handler),
	{}
};

struct kunit_suite autobahn_signal_in_irq_test_suite = {
	.name = "autobahn_signal_in_irq_test_suite",
	.test_cases = autobahn_signal_in_irq_test_cases,
	.init = autobahn_signal_in_init,
	.exit = autobahn_signal_in_exit,
};

kunit_test_suites(&autobahn_signal_in_create_test_suite,
		  &autobahn_signal_in_irq_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alina Croitor");

