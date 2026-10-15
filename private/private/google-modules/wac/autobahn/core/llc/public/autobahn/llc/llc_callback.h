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

// This API is going to be called from the Proxy context
// in order to execute the pending callbacks
typedef void (*abn_llc_Llc_ExecuteCallbackNotifications)(
    void* container_handle);

struct abn_llc_Llc_Callbacks_Functions {
  abn_llc_Llc_ExecuteCallbackNotifications execute_callbacks;
};

struct abn_llc_Llc_Callback_Interface {
  const struct abn_llc_Llc_Callbacks_Functions* functions;
  void* llc_handle;
};

ABN_EXTERN_C_END
