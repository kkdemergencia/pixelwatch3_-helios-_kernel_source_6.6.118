// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google, Inc.
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
#include <linux/types.h>

#include "autobahn/platform/linux/checksum.h"

static void test_crc16_ccitt(struct kunit *test)
{
	u32 input_buffer = 0xABCDEF01;
	u16 crc = abn_crc16_ccitt((u8 *)&input_buffer, sizeof(u32));

	KUNIT_EXPECT_EQ(test, crc, 0x1AFD);
}

static void test_crc32(struct kunit *test)
{
	u32 input_buffer = 0xABCDEF01;
	u16 crc = abn_crc32((u8 *)&input_buffer, sizeof(u32));

	KUNIT_EXPECT_EQ(test, crc, 0xF891);
}

struct kunit_case autobahn_checksum_test_cases[] = {
	KUNIT_CASE(test_crc16_ccitt),
	KUNIT_CASE(test_crc32),
	{}
};

struct kunit_suite autobahn_checksum_test_suite = {
	.name = "autobahn_checksum_test_suite",
	.test_cases = autobahn_checksum_test_cases
};

kunit_test_suites(&autobahn_checksum_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Radu Dobroiu");
