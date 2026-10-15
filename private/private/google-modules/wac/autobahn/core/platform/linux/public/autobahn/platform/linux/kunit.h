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
#include <linux/compiler_attributes.h>
#include <linux/kconfig.h>

/* Allow mocking functions in kunit tests. Adding the __mockable attribute to
 * a symbols defines it __weak, so that it may be overridden by the test
 * module.
 *
 * Usage consists of creating a wrapper to your function, decorated with the
 * __mockable attribute:
 *
 *   int __mockable my_func_wrapper()
 *   {
 *     return my_func();
 *   }
 *
 * If CONFIG_AUTOBAHN_TESTS is disabled, the wrapper is forced inline to
 * eliminate any runtime overhead.
 */
#if IS_ENABLED(CONFIG_AUTOBAHN_TESTS)
#define __mockable __weak
#else
#define __mockable __always_inline
#endif
