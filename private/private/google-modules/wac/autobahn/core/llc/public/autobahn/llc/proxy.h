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

#include "autobahn/llc/llc_callback.h"
#include "autobahn/llc/proxy_interface.h"
#include "autobahn/log/log.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/sync/thread_notification.h"

struct abn_llc_Llc_Proxy_Config {
  abn_sync_ThreadNotification* event_notification;

  struct abn_llc_Llc_Callback_Interface* llc_callback;
};

struct abn_llc_Llc_Proxy {
  const struct abn_llc_Llc_Proxy_Config* config;
};

ABN_EXTERN_C_START

void abn_llc_Llc_Proxy_Init(struct abn_llc_Llc_Proxy* proxy,
                            const struct abn_llc_Llc_Proxy_Config* config);

void abn_llc_Llc_Proxy_Deinit(struct abn_llc_Llc_Proxy* proxy);

static inline void abn_llc_Llc_Proxy_TriggerEventNotification(void* context) {
  struct abn_llc_Llc_Proxy* proxy = (struct abn_llc_Llc_Proxy*)context;
  ABN_ASSERT(proxy != NULL);

  abn_sync_ThreadNotification_Release(proxy->config->event_notification);
}

void abn_llc_Llc_Proxy_ProcessAllNotificationsAndWaitForNextEvent(
    struct abn_llc_Llc_Proxy* proxy);

void abn_llc_Llc_Proxy_ProcessAllNotifications(struct abn_llc_Llc_Proxy* proxy);

ABN_EXTERN_C_END

#ifdef __cplusplus

namespace abn::llc {

// The LLC proxy is a component that is used to proxy LLC events to the
// application.
class Proxy {
 public:
  Proxy(abn_llc_Llc_Callback_Interface& llc_callback)
      : config_{.event_notification = &event_notification_,
                .llc_callback = &llc_callback} {}

  void Enable() { abn_llc_Llc_Proxy_Init(&proxy_, &config_); }
  void Disable() { abn_llc_Llc_Proxy_Deinit(&proxy_); }

  void TriggerEventNotification() {
    abn_llc_Llc_Proxy_TriggerEventNotification(&proxy_);
  }

  void ProcessAllNotificationsAndWaitForNextEvent() {
    abn_llc_Llc_Proxy_ProcessAllNotificationsAndWaitForNextEvent(&proxy_);
  }

  void ProcessAllNotifications() {
    abn_llc_Llc_Proxy_ProcessAllNotifications(&proxy_);
  }

  abn_llc_Llc_Proxy& c_handle() { return proxy_; }

 private:
  static inline constexpr abn_llc_Llc_Proxy_Client_Functions
      kLlcProxyFunctionTable = {
          .trigger_event_notification =
              abn_llc_Llc_Proxy_TriggerEventNotification,
  };

  const abn_llc_Llc_Proxy_Config config_;

  abn_llc_Llc_Proxy proxy_;

  abn_sync_ThreadNotification event_notification_;
};

}  // namespace abn::llc

#endif  // defined(__cplusplus)
