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

#include <linux/panic.h>
#include <linux/printk.h>

#define ABN_LOG_DEBUG(fmt, ...) \
  pr_debug("%s:%s: " fmt, KBUILD_MODNAME, __func__, ##__VA_ARGS__)
#define ABN_LOG_INFO(fmt, ...) \
  pr_info("%s:%s: " fmt, KBUILD_MODNAME, __func__, ##__VA_ARGS__)
#define ABN_LOG_WARN(fmt, ...) \
  pr_warn("%s:%s: " fmt, KBUILD_MODNAME, __func__, ##__VA_ARGS__)
#define ABN_LOG_ERROR(fmt, ...) \
  pr_err("%s:%s: " fmt, KBUILD_MODNAME, __func__, ##__VA_ARGS__)
#define ABN_LOG_CRITICAL(fmt, ...) \
  WARN(1, "%s:%s: " fmt, KBUILD_MODNAME, __func__, ##__VA_ARGS__)
#define ABN_LOG_FATAL(fmt, ...) \
  panic("%s:%s: " fmt, KBUILD_MODNAME, __func__, ##__VA_ARGS__)
