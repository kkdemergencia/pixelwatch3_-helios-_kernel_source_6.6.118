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

#include "autobahn/preprocessor/util.h"

ABN_EXTERN_C_START

enum abn_Signal_State {
  ABN_SIGNAL_STATE_ACTIVE = 1,
  ABN_SIGNAL_STATE_INACTIVE = 2,
  ABN_SIGNAL_STATE_UNKNOWN = 3,
};

ABN_EXTERN_C_END
