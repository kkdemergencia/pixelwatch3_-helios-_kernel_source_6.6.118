// SPDX-License-Identifier: GPL-2.0-only
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

#include "autobahn/llc/proxy.h"

#include "autobahn/llc/proxy_interface.h"

void abn_llc_Llc_Proxy_Init(struct abn_llc_Llc_Proxy* proxy,
                            const struct abn_llc_Llc_Proxy_Config* config) {
  ABN_ASSERT(proxy != NULL);

  proxy->config = config;
}

void abn_llc_Llc_Proxy_Deinit(struct abn_llc_Llc_Proxy* proxy) {
  ABN_ASSERT(proxy != NULL);
}

void abn_llc_Llc_Proxy_ProcessAllNotificationsAndWaitForNextEvent(
    struct abn_llc_Llc_Proxy* proxy) {
  ABN_ASSERT(proxy != NULL);

  proxy->config->llc_callback->functions->execute_callbacks(
      proxy->config->llc_callback->llc_handle);

  // Wait for the next event.
  abn_sync_ThreadNotification_Acquire(proxy->config->event_notification);
}

void abn_llc_Llc_Proxy_ProcessAllNotifications(
    struct abn_llc_Llc_Proxy* proxy) {
  if (proxy == NULL) {
    return;
  }

  proxy->config->llc_callback->functions->execute_callbacks(
      proxy->config->llc_callback->llc_handle);
}
