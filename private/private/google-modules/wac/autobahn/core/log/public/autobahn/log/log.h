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

// This header must provide all of the log macros:
//  ABN_LOG_DEBUG(fmt, ...)
//  ABN_LOG_INFO(fmt, ...)
//  ABN_LOG_WARN(fmt, ...)
//  ABN_LOG_ERROR(fmt, ...)
//  ABN_LOG_CRITICAL(fmt, ...)
//  ABN_LOG_FATAL(fmt, ...)
//
// Usage & backend implementation requirements:
// - Logging shall only be done in source files and not in headers.
// - Logging shall be safe in interrupt contexts, this may mean dropping
//   non-fatal logs and translating fatal logs into asserts.
// - Backends are responsible for controlling what logs are compiled out.
#include "autobahn/log/log_backend/log.h"

#ifndef ABN_LOG_DEBUG
#error "ABN_LOG_DEBUG must be provided"
#endif  // !defined(ABN_LOG_DEBUG)

#ifndef ABN_LOG_INFO
#error "ABN_LOG_INFO must be provided"
#endif  // !defined(ABN_LOG_INFO)

#ifndef ABN_LOG_WARN
#error "ABN_LOG_WARN must be provided"
#endif  // !defined(ABN_LOG_WARN)

#ifndef ABN_LOG_ERROR
#error "ABN_LOG_ERROR must be provided"
#endif  // !defined(ABN_LOG_ERROR)

#ifndef ABN_LOG_CRITICAL
#error "ABN_LOG_CRITICAL must be provided"
#endif  // !defined(ABN_LOG_CRITICAL)

#ifndef ABN_LOG_FATAL
#error "ABN_LOG_FATAL must be provided"
#endif  // !defined(ABN_LOG_FATAL)
