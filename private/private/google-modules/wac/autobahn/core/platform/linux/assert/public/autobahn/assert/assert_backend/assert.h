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

#include <linux/bug.h>
#include <linux/build_bug.h>

#define ABN_STATIC_ASSERT static_assert

#define ABN_ASSERT(condition) BUG_ON(!(condition))

#ifdef CONFIG_AUTOBAHN_DASSERT
#  define ABN_DASSERT ABN_ASSERT
#else
#  define ABN_DASSERT(condition) (void)(condition)
#endif

/* Prefer WARN() instead of BUG_ON() where possible. */
#define ABN_DEBUG_ASSERT_ENABLED 0
