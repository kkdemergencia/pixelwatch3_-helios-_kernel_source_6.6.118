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
#include <linux/gpio.h>
#include "autobahn/platform/linux/signal_out.h"
#include "autobahn/signal.h"
#include "autobahn/signal_out.h"

#define GPIO_HANDLE ((struct gpio_desc *) 0xdeadbeef)

static void create_destroy(struct kunit *test)
{
	struct abn_SignalOut *abn_SignalOut = NULL;

	KUNIT_EXPECT_EQ(test, 0, abn_SignalOut_Create(&abn_SignalOut, GPIO_HANDLE));

	KUNIT_EXPECT_TRUE(test, abn_SignalOut != NULL);
	KUNIT_EXPECT_TRUE(test, abn_SignalOut->signal_out_handle != NULL);
	KUNIT_EXPECT_TRUE(test, abn_SignalOut->functions->set != NULL);

	abn_SignalOut_Destroy(abn_SignalOut);
}

struct kunit_case autobahn_signalOut_test_cases[] = {
	KUNIT_CASE(create_destroy),
	{}
};

struct kunit_suite autobahn_signal_out_test_suite = {
	.name = "autobahn_signal_out_test_suite",
	.test_cases = autobahn_signalOut_test_cases
};

kunit_test_suites(&autobahn_signal_out_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alina Croitor");

