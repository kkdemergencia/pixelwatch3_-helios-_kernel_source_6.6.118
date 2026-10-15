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

// The backend shall provide methods commonly provided by the standard string
// library:
//   void* memcpy(void* destination, const void* source, size_t num)
//   int memcmp(const void* ptr1, const void* ptr2, size_t num)
//
// In an environment that provides a standard library implementation, the
// backend can simply include standard library headers:
//   #include <string.h>
#include "autobahn/string_backend/string.h"
