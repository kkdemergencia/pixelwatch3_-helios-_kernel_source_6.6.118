/* SPDX-License-Identifier: GPL-2.0-only */
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

#pragma once

// This header must provide the following assert macro:
//   ABN_ASSERT(condition)
//   ABN_DASSERT(condition)
//   ABN_STATIC_ASSERT(condition, message)
//
// Usage & backend implementation requirements:
// - Asserts shall be used in both source and header files.
// - Asserts shall not be used in constexpr contexts.
// - Backends are responsible for controlling whether ABN_DASSERT is compiled
//   out.
#include "autobahn/assert/assert_backend/assert.h"

#ifndef ABN_ASSERT
#error "ABN_ASSERT must be provided"
#endif  // !defined(ABN_ASSERT)

#ifndef ABN_DASSERT
#error "ABN_DASSERT must be provided"
#endif  // !defined(ABN_DASSERT)

#ifndef ABN_STATIC_ASSERT
#error "ABN_STATIC_ASSERT must be provided"
#endif

// TODO: b/398947466 - Define ABN_DEBUG_ASSERT_ENABLED by the platform
#ifndef ABN_DEBUG_ASSERT_ENABLED
#define ABN_DEBUG_ASSERT_ENABLED 1
#endif
