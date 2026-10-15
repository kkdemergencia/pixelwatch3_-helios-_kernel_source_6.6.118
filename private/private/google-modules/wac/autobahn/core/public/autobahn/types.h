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

#include "autobahn/assert/assert.h"

// The backend shall provide types commonly provided by the standard library:
//   bool
//   size_t
//   uint8_t
//   uint16_t
//   uint32_t
//   int8_t
//   int16_t
//   int32_t
//
// In an environment that provides a standard library implementation, the
// backend can simply include standard library headers:
//   #include <stdbool.h>
//   #include <stdint.h>
//   #include <stddef.h>
#include "autobahn/types_backend/types.h"

ABN_STATIC_ASSERT(sizeof(bool) > 0, "bool type must be provided");
ABN_STATIC_ASSERT(sizeof(size_t) >= 2, "size_t type must be provided");
ABN_STATIC_ASSERT(sizeof(uint8_t) == 1, "uint8_t type must be provided");
ABN_STATIC_ASSERT(sizeof(uint16_t) == 2, "uint16_t type must be provided");
ABN_STATIC_ASSERT(sizeof(uint32_t) == 4, "uint32_t type must be provided");
ABN_STATIC_ASSERT(sizeof(int8_t) == 1, "int8_t type must be provided");
ABN_STATIC_ASSERT(sizeof(int16_t) == 2, "int16_t type must be provided");
ABN_STATIC_ASSERT(sizeof(int32_t) == 4, "int32_t type must be provided");
