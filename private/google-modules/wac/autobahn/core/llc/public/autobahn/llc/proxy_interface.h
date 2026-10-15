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

ABN_EXTERN_C_START

// This API is going to be called from a different context in order to
// wake up Proxy thread and execute some tasks on that context
typedef void (*abn_llc_Llc_Proxy_Client_TriggerEventNotification)(
    void* proxy_handle);

struct abn_llc_Llc_Proxy_Client_Functions {
  abn_llc_Llc_Proxy_Client_TriggerEventNotification trigger_event_notification;
};

struct abn_llc_Llc_Proxy_Client {
  const struct abn_llc_Llc_Proxy_Client_Functions* functions;
  void* proxy_handle;
};

ABN_EXTERN_C_END
