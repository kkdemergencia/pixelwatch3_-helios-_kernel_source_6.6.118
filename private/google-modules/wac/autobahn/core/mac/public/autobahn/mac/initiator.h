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

// TODO: b/313653240 - consider merging the transaction done timer and the
// responder done timer expired event into one given that it's backed by a
// singular timer usage (E.g. EVENT_TRANSACTION_TIMER_EXPIRED)
enum abn_mac_Initiator_Event {
  ABN_MAC_INITIATOR_EVENT_NONE = 0,
  ABN_MAC_INITIATOR_EVENT_TX_DATA_AVAILABLE,
  ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_WHILE_IN_IDLE,
  ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_WHILE_WAITING_FOR_RESPONDER_READY,
  ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_WHILE_WAITING_FOR_RESPONDER_DONE,
  ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_SUCCESS_WHILE_WAITING_FOR_TRANSFER_DONE,
  ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_ERROR_WHILE_WAITING_FOR_TRANSFER_DONE,
  ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE,
  ABN_MAC_INITIATOR_EVENT_TRANSFER_ERROR_AND_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE,
  ABN_MAC_INITIATOR_EVENT_TRANSFER_SUCCESS_AND_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE,
  ABN_MAC_INITIATOR_EVENT_TRANSFER_ERROR_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE,
  ABN_MAC_INITIATOR_EVENT_TRANSFER_SUCCESS_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE,
  ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_TIMER_EXPIRED,
  ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_TIMER_EXPIRED,
  ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_TIMER_EXPIRED,
  ABN_MAC_INITIATOR_EVENT_INVALID_STATE
};

// Initiator states
enum abn_mac_Initiator_State {
  ABN_MAC_INITIATOR_STATE_UNINITIALIZED = 0,
  ABN_MAC_INITIATOR_STATE_IDLE,
  ABN_MAC_INITIATOR_STATE_WAITING_FOR_RESPONDER_READY,
  ABN_MAC_INITIATOR_STATE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE,
  ABN_MAC_INITIATOR_STATE_WAITING_FOR_RESPONDER_DONE,
  ABN_MAC_INITIATOR_STATE_WAITING_FOR_TRANSFER_DONE,
};

// Initiator client callback opcodes
enum abn_mac_Initiator_CallbackOpcode {
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_TRANSACTION_SUCCESS,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_TRANSACTION_ERROR,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_TIMER_EXPIRED,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_INVALID_STATE,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_RESPONDER_NOT_RESPONDING,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_INITIATE_TRANSFER,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_SIGNAL_ACTIVE_FROM_RESPONDER,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_SIGNAL_INACTIVE_FROM_RESPONDER,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_PREPARE_BUFFERS_START,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_PREPARE_BUFFERS_END,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_PROCESS_BUFFERS_START,
  ABN_MAC_INITIATOR_CALLBACK_OPCODE_PROCESS_BUFFERS_END,
};

enum abn_mac_Initiator_TransportResult {
  ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE = 0,
  ABN_MAC_INITIATOR_TRANSPORT_RESULT_SUCCESS = 1,
  ABN_MAC_INITIATOR_TRANSPORT_RESULT_ERROR = 2
};

enum abn_mac_Initiator_PendingEvents {
  ABN_MAC_INITIATOR_PENDING_EVENT_TRANSFER_RESULT = 0x01,
  ABN_MAC_INITIATOR_PENDING_EVENT_RESPONDER_READY = 0x02,
  ABN_MAC_INITIATOR_PENDING_EVENT_RESPONDER_DONE = 0x04,
  ABN_MAC_INITIATOR_PENDING_EVENT_TIMER_EXPIRED = 0x08,
};

// flags - extra information, based on opcode value
// for (opcode == INVALID_STATE), flags contain all
// abn_mac_Initiator_PendingEvents
typedef void (*abn_mac_Initiator_Callback)(
    void* context,
    enum abn_mac_Initiator_CallbackOpcode opcode,
    enum abn_mac_Initiator_State state,
    uint32_t flags);

struct abn_mac_Initiator_Config {
  // Transport object
  struct abn_Transport* transport;

  // Signal to responder object
  struct abn_SignalOut* signal_to_responder;

  // Signal from responder object
  struct abn_SignalIn* signal_from_responder;

  // signal object used for ABN thread
  abn_sync_ThreadNotification* event_notification;

  // Internal Atomic object passed from upper layer
  abn_sync_Atomic* handling_responder_signal_changed_event;

  // Internal Atomic object passed from upper layer
  abn_sync_Atomic* transaction_result;

  // Internal Atomic object passed from upper layer
  abn_sync_Atomic* timer_expired;

  // Internal Atomic object passed from upper layer
  // current state of MAC state machine
  abn_sync_Atomic* state;

  // Client callback
  abn_mac_Initiator_Callback client_callback;
  void* client_callback_context;

  // timer used for timing out of pending events
  struct abn_Timer* timer;

  // timeout value from initiating a transaction
  // until the transaction is done and Responder INACTIVE
  // was received
  abn_Timer_Ticks timeout_transaction_done;

  // timeout value used when Initiator signal to Responder
  // about starting a transaction
  abn_Timer_Ticks timeout_responder_ready;

  // Use for protecting the idle callback
  abn_sync_Mutex* mutex_callback;

  // set to 1 while ProcessEvent is called
  // this is used to avoid a race condition in IsIdle()
  abn_sync_Atomic* processing_event;

  // Maxim of consecutive Responder signaling timeouts
  // before reporting ABN_MAC_INITIATOR_CALLBACK_OPCODE_RESPONDER_NOT_RESPONDING
  size_t max_consecutive_responder_timeout_retries;
};

struct abn_mac_Initiator {
  // contains all Initiator configuration coming from upper layer
  const struct abn_mac_Initiator_Config* config;

  // interface with data layer
  const struct abn_mac_Client* data_client;

  // TX/RX current transaction buffers
  struct abn_mac_Frame* tx_data;
  struct abn_mac_Frame* rx_data;

  // true when prepare_buffers() has been called but neither process_rx() nor
  // handle_transaction_failure() have been called.
  bool client_transaction_pending;

  bool responder_signal_changed_to_active;
  bool responder_signal_changed_to_inactive;

  enum abn_Signal_State last_responder_signal_state;

  bool responder_signal_changed;

  abn_mac_Pm_IdleCallback idle_callback;
  void* idle_callback_context;

  enum abn_mac_Pm_IdleState idle_state_reported;

  // Current number of consecutive Responder siganling
  // if this is >= config->maxim_consecutive_responder_timeout_retries
  // we send ABN_MAC_INITIATOR_CALLBACK_OPCODE_RESPONDER_NOT_RESPONDING
  size_t consecutive_responder_timeout_counter;
};

ABN_EXTERN_C_START

// Initialize MAC object
void abn_mac_Initiator_Init(struct abn_mac_Initiator* initiator,
                            const struct abn_mac_Initiator_Config* config,
                            const struct abn_mac_Client* data_client);

// Deinitialize MAC object
// The argument should point to a valid abn_mac_Initiator object which was
// already initialized
void abn_mac_Initiator_Deinit(struct abn_mac_Initiator* initiator);

// This is the implementation of
// abn_mac_Mac_Functions.abn_mac_Mac_SignalTxDataAvailable. This is consumed by
// abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent to detect new events.
//
// Instantiators are permitted to use this to enable thread joining for testing
// or kernel module teardown.
static inline void abn_mac_Initiator_EventNotification(void* context) {
  struct abn_mac_Initiator* initiator = (struct abn_mac_Initiator*)context;
  abn_sync_ThreadNotification_Release(initiator->config->event_notification);
}

// The actual working portions of the MAC thread, the expectation is to call
// abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent, this function is
// exposed purely for unit testing.
//
// Returns true if an event was processed, false if there were no more events to
// process.
//
// Preconditions:
// - The initiator must already be initialized.
bool abn_mac_Initiator_ProcessOneEvent(struct abn_mac_Initiator* initiator);

// This is the recommended step to be called in a loop on a separate thread.
// The argument should point to a valid abn_mac_Initiator object which was
// already initialized.
void abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent(
    struct abn_mac_Initiator* initiator);

// Return true if Initiator is in IDLE, false otherwise
bool abn_mac_Initiator_IsIdle(void* context);

// Set IDLE callback
void abn_mac_Initiator_SetIdleCallback(void* container_handle,
                                       abn_mac_Pm_IdleCallback callback,
                                       void* context);

ABN_EXTERN_C_END

#ifdef __cplusplus

namespace abn::mac {

class Initiator {
 public:
  explicit Initiator(abn_Transport& transport,
                     abn_SignalOut& signal_to_responder,
                     abn_SignalIn& signal_from_responder,
                     abn_mac_Client& data_client,
                     abn_mac_Initiator_Callback callback,
                     void* callback_context,
                     abn_Timer& timer,
                     abn_Timer_Ticks timeout_transaction_done,
                     abn_Timer_Ticks timeout_responder_ready,
                     size_t max_responder_not_responding_retries)
      : config_{.transport = &transport,
                .signal_to_responder = &signal_to_responder,
                .signal_from_responder = &signal_from_responder,
                .event_notification = &event_notification_,
                .handling_responder_signal_changed_event =
                    &responder_signal_changed_,
                .transaction_result = &transaction_result_,
                .timer_expired = &timer_expired_,
                .state = &state_,
                .client_callback = callback,
                .client_callback_context = callback_context,
                .timer = &timer,
                .timeout_transaction_done = timeout_transaction_done,
                .timeout_responder_ready = timeout_responder_ready,
                .mutex_callback = &mutex_callback_,
                .processing_event = &processing_event_,
                .max_consecutive_responder_timeout_retries =
                    max_responder_not_responding_retries},
        mac_interface_{&kMacInitiatorFunctionTable, &initiator_},
        data_client_(data_client) {}

  void Enable() {
    abn_mac_Initiator_Init(&initiator_, &config_, &data_client_);
  }
  void Disable() { abn_mac_Initiator_Deinit(&initiator_); }

  void EventNotification() { abn_mac_Initiator_EventNotification(&initiator_); }

  bool ProcessOneEvent() {
    return abn_mac_Initiator_ProcessOneEvent(&initiator_);
  }

  void ProcessAllEventsAndWaitForNextEvent() {
    abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent(&initiator_);
  }

  abn_mac_Initiator& c_handle() { return initiator_; }

  const abn_mac_Mac& mac_interface() { return mac_interface_; }

 protected:
  abn_mac_Initiator initiator_;

 private:
  const abn_mac_Initiator_Config config_;
  const abn_mac_Mac mac_interface_;

  abn_mac_Client& data_client_;

  // wait signal
  abn_sync_ThreadNotification event_notification_;

  // Internal Atomic object
  abn_sync_Atomic responder_signal_changed_;

  // Internal Atomic object
  abn_sync_Atomic timer_expired_;

  // Internal Atomic object
  abn_sync_Atomic transaction_result_;

  // Internal Atomic object
  abn_sync_Atomic state_;

  abn_sync_Atomic processing_event_;

  abn_sync_Mutex mutex_callback_;

  static inline constexpr abn_mac_Mac_Functions kMacInitiatorFunctionTable = {
      .signal_tx_data_available = abn_mac_Initiator_EventNotification,
  };

  static inline constexpr abn_mac_Pm_Functions kMacPmInitiatorFunctionTable = {
      .is_idle = abn_mac_Initiator_IsIdle,
      .set_idle_callback = abn_mac_Initiator_SetIdleCallback,
  };
};

}  // namespace abn::mac

#endif  // defined(__cplusplus)
