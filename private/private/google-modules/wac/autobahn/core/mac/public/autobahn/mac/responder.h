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

#include "autobahn/mac/client.h"
#include "autobahn/mac/mac.h"
#include "autobahn/mac/mac_pm.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/signal_in.h"
#include "autobahn/signal_out.h"
#include "autobahn/sync/atomic.h"
#include "autobahn/sync/mutex.h"
#include "autobahn/sync/thread_notification.h"
#include "autobahn/timer.h"
#include "autobahn/transport.h"
#include "autobahn/types.h"

// Responder specific events on top of generic ones
enum abn_mac_Responder_Event {
  ABN_MAC_RESPONDER_EVENT_NONE = 0,
  ABN_MAC_RESPONDER_EVENT_TX_DATA_AVAILABLE,
  ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_SUCCESS,
  ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_ERROR,
  ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_IDLE,
  ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_WAITING,
  ABN_MAC_RESPONDER_EVENT_INITIATOR_DONE,
  ABN_MAC_RESPONDER_EVENT_INVALID_STATE,
  ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_TIMER_EXPIRED,
  ABN_MAC_RESPONDER_EVENT_INITIATOR_DONE_TIMER_EXPIRED,
  ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_TIMER_EXPIRED,
  ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_WAITING_FOR_INITIATOR_DONE
};

enum abn_mac_Responder_State {
  ABN_MAC_RESPONDER_STATE_UNINITIALIZED = 0,
  ABN_MAC_RESPONDER_STATE_IDLE,
  ABN_MAC_RESPONDER_STATE_WAITING_FOR_INITIATOR_READY,
  ABN_MAC_RESPONDER_STATE_TRANSFER_IN_PROGRESS,
  ABN_MAC_RESPONDER_STATE_WAITING_FOR_INITIATOR_DONE,
};

enum abn_mac_Responder_CallbackOpcode {
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_TRANSFER_SUCCESS,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_TRANSFER_ERROR,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_INVALID_STATE,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_TIMER_EXPIRED,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_INITIATOR_SIGNAL_INACTIVE_MISSED,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_SIGNAL_ACTIVE_TO_INITIATOR,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_SIGNAL_INACTIVE_TO_INITIATOR,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_SIGNAL_INACTIVE_FROM_INITIATOR,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_PREPARE_BUFFERS_START,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_PREPARE_BUFFERS_END,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_PROCESS_BUFFERS_START,
  ABN_MAC_RESPONDER_CALLBACK_OPCODE_PROCESS_BUFFERS_END,
};

enum abn_mac_Responder_TransferResult {
  ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE = 0,
  ABN_MAC_RESPONDER_TRANSFER_RESULT_SUCCESS,
  ABN_MAC_RESPONDER_TRANSFER_RESULT_ERROR
};

enum abn_mac_Responder_PendingEvents {
  ABN_MAC_RESPONDER_PENDING_EVENT_TRANSFER_RESULT = 0x01,
  ABN_MAC_RESPONDER_PENDING_EVENT_INITIATOR_READY = 0x02,
  ABN_MAC_RESPONDER_PENDING_EVENT_INITIATOR_DONE = 0x04,
  ABN_MAC_RESPONDER_PENDING_EVENT_TIMER_EXPIRED = 0x08,
};

typedef void (*abn_mac_Responder_Callback)(
    void* context,
    enum abn_mac_Responder_CallbackOpcode opcode,
    enum abn_mac_Responder_State state,
    uint32_t flags);

struct abn_mac_Responder_Config {
  // Transport interface
  struct abn_Transport* transport;

  // Signal to Initiator interface
  struct abn_SignalOut* signal_to_initiator;

  // Signal from Initiator interface
  struct abn_SignalIn* signal_from_initiator;

  // Main callback
  abn_mac_Responder_Callback client_callback;
  void* client_callback_context;

  // signal object used for ABN thread
  abn_sync_ThreadNotification* event_notification;

  // Internal Atomic objects passed from upper layer
  abn_sync_Atomic* transfer_result;
  abn_sync_Atomic* initiator_signal_transitioned;
  abn_sync_Atomic* timer_expired;

  // current state of MAC state machine
  abn_sync_Atomic* state;

  // timer used for timing out of pending transaction
  struct abn_Timer* timer;

  // timeout value from preparing a transaction
  // until the transaction is done
  abn_Timer_Ticks timeout_transfer_result;

  // timeout value until Initiator signal ready is received
  abn_Timer_Ticks timeout_initiator_ready;

  // timeout value until Initiator signal done is received
  abn_Timer_Ticks timeout_initiator_done;

  // set to 1 while ProcessEvent is called
  // this is used to avoid a race condition in IsIdle()
  abn_sync_Atomic* processing_event;
};

struct abn_mac_Responder {
  // contains all Responder configuration coming from upper layer
  const struct abn_mac_Responder_Config* config;

  // interface with data layer
  const struct abn_mac_Client* data_client;

  // Tx/RX current transaction buffers
  struct abn_mac_Frame* tx_data;
  struct abn_mac_Frame* rx_data;

  // true when prepare_buffers() has been called but neither process_rx() nor
  // handle_transaction_failure() have been called.
  bool client_transaction_pending;
  enum abn_Signal_State last_initiator_signal_state;

  bool initiator_signal_transitioned_and_is_now_active;
  bool initiator_signal_transitioned_and_is_now_inactive;

  abn_mac_Pm_IdleCallback idle_callback;
  void* idle_callback_context;

  enum abn_mac_Pm_IdleState idle_state_reported;
};

ABN_EXTERN_C_START

// Init MAC object
void abn_mac_Responder_Init(struct abn_mac_Responder* responder,
                            const struct abn_mac_Responder_Config* config,
                            const struct abn_mac_Client* data_client);

// Deinit MAC object
// The argument should point to a valid abn_mac_Responder object which was
// already initialized
void abn_mac_Responder_Deinit(struct abn_mac_Responder* responder);

// This is the implementation of
// abn_mac_Mac_Functions.abn_mac_Mac_SignalTxDataAvailable. This is consumed by
// abn_mac_Responder_ProcessAllEventsAndWaitForNextEvent to detect new events.
//
// Instantiators are permitted to use this to enable thread joining for testing
// or kernel module teardown.
static inline void abn_mac_Responder_EventNotification(void* context) {
  struct abn_mac_Responder* responder = (struct abn_mac_Responder*)context;
  abn_sync_ThreadNotification_Release(responder->config->event_notification);
}

// The actual working portions of the MAC thread, the expectation is to call
// abn_mac_Responder_ProcessAllEventsAndWaitForNextEvent, this function is
// exposed purely for unit testing.
//
// Returns true if an event was processed, false if there were no more events to
// process.
//
// Preconditions:
// - The responder must already be initialized.
bool abn_mac_Responder_ProcessOneEvent(struct abn_mac_Responder* responder);

// This is the recommended step to be called in a loop on a separate thread.
// The argument should point to a valid abn_mac_Responder object which was
// already initialized.
void abn_mac_Responder_ProcessAllEventsAndWaitForNextEvent(
    struct abn_mac_Responder* responder);

// Return true if Responder is in IDLE, false otherwise
bool abn_mac_Responder_IsIdle(void* context);

// Set IDLE callback
void abn_mac_Responder_SetIdleCallback(void* container_handle,
                                       abn_mac_Pm_IdleCallback callback,
                                       void* context);

ABN_EXTERN_C_END

#ifdef __cplusplus

namespace abn::mac {

class Responder {
 public:
  Responder(abn_Transport& transport,
            abn_SignalOut& signal_to_initiator,
            abn_SignalIn& signal_from_initiator,
            abn_mac_Client& data_client,
            abn_mac_Responder_Callback callback,
            void* callback_context,
            abn_Timer& timer,
            abn_Timer_Ticks timeout_transfer_result,
            abn_Timer_Ticks timeout_initiator_ready,
            abn_Timer_Ticks timeout_initiator_done)
      : config_{
            .transport = &transport,
            .signal_to_initiator = &signal_to_initiator,
            .signal_from_initiator = &signal_from_initiator,
            .client_callback = callback,
            .client_callback_context = callback_context,
            .event_notification = &event_notification_,
            .transfer_result = &transfer_result_,
            .initiator_signal_transitioned = &initiator_signal_transitioned_,
            .timer_expired = &timer_expired_,
            .state = &state_,
            .timer = &timer,
            .timeout_transfer_result = timeout_transfer_result,
            .timeout_initiator_ready = timeout_initiator_ready,
            .timeout_initiator_done = timeout_initiator_done,
            .processing_event = &processing_event_},
        mac_interface_{&kMacResponderFunctionTable, &responder_},
        mac_pm_{&kMacPmResponderFunctionTable, &responder_},
        data_client_(data_client) {}

  void Enable() {
    abn_mac_Responder_Init(&responder_, &config_, &data_client_);
  }
  void Disable() { abn_mac_Responder_Deinit(&responder_); }

  void EventNotification() { abn_mac_Responder_EventNotification(&responder_); }

  bool ProcessOneEvent() {
    return abn_mac_Responder_ProcessOneEvent(&responder_);
  }

  void ProcessAllEventsAndWaitForNextEvent() {
    abn_mac_Responder_ProcessAllEventsAndWaitForNextEvent(&responder_);
  }

  bool PmIsIdle() { return mac_pm_.functions->is_idle(&responder_); }

  abn_mac_Responder& c_handle() { return responder_; }

  const abn_mac_Mac& mac_interface() { return mac_interface_; }

  const abn_mac_Pm& mac_pm() { return mac_pm_; }

 private:
  const abn_mac_Responder_Config config_;
  abn_mac_Mac mac_interface_;
  abn_mac_Pm mac_pm_;

  abn_mac_Client& data_client_;

  abn_mac_Responder responder_;

  abn_sync_ThreadNotification event_notification_;

  // Internal Atomic objects
  abn_sync_Atomic transfer_result_;
  abn_sync_Atomic initiator_signal_transitioned_;
  abn_sync_Atomic timer_expired_;
  abn_sync_Atomic state_;
  abn_sync_Atomic processing_event_;

  static inline constexpr abn_mac_Mac_Functions kMacResponderFunctionTable = {
      .signal_tx_data_available = abn_mac_Responder_EventNotification,
  };

  static inline constexpr abn_mac_Pm_Functions kMacPmResponderFunctionTable = {
      .is_idle = abn_mac_Responder_IsIdle,
      .set_idle_callback = abn_mac_Responder_SetIdleCallback,
  };
};

}  // namespace abn::mac

#endif  // defined(__cplusplus)
