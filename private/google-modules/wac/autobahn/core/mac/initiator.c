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

#include "autobahn/mac/initiator.h"

#include "autobahn/assert/assert.h"
#include "autobahn/autobahn.h"
#include "autobahn/log/log.h"
#include "autobahn/signal_in.h"
#include "autobahn/signal_out.h"
#include "autobahn/sync/atomic.h"
#include "autobahn/sync/thread_notification.h"
#include "autobahn/timer.h"
#include "autobahn/transport.h"

static void abn_mac_Initiator_TransportResultHandler(void* context,
                                                     bool success) {
  struct abn_mac_Initiator* initiator = (struct abn_mac_Initiator*)context;
  ABN_ASSERT(initiator != NULL);

  if (success) {
    abn_sync_Atomic_StoreRelease(initiator->config->transaction_result,
                                 ABN_MAC_INITIATOR_TRANSPORT_RESULT_SUCCESS);
  } else {
    abn_sync_Atomic_StoreRelease(initiator->config->transaction_result,
                                 ABN_MAC_INITIATOR_TRANSPORT_RESULT_ERROR);
  }

  // Notify the MAC thread
  abn_sync_ThreadNotification_Release(initiator->config->event_notification);
}

static void abn_mac_Initiator_ResponderSignalTransitionHandler(void* context) {
  struct abn_mac_Initiator* initiator = (struct abn_mac_Initiator*)context;

  ABN_ASSERT(initiator != NULL);

  abn_sync_Atomic_StoreRelease(
      initiator->config->handling_responder_signal_changed_event, 1);

  // Notify the MAC thread
  abn_sync_ThreadNotification_Release(initiator->config->event_notification);
}

static void abn_mac_Initiator_TimerExpiryHandler(void* context) {
  struct abn_mac_Initiator* initiator = (struct abn_mac_Initiator*)context;

  ABN_ASSERT(initiator != NULL);

  abn_sync_Atomic_StoreRelease(initiator->config->timer_expired, 1);
  // Notify the MAC thread
  abn_sync_ThreadNotification_Release(initiator->config->event_notification);
}

static enum abn_mac_Initiator_Event abn_mac_Initiator_ResponderSignalHandle(
    struct abn_mac_Initiator* initiator) {
  enum abn_Signal_State responder_signal_state;

  initiator->responder_signal_changed_to_active = false;
  initiator->responder_signal_changed_to_inactive = false;

  initiator->responder_signal_changed =
      (abn_sync_Atomic_ExchangeAcquire(
           initiator->config->handling_responder_signal_changed_event, 0) != 0);
  if (initiator->responder_signal_changed) {
    // Check if the responder signal transitioned while getting the
    // responder state.
    responder_signal_state =
        initiator->config->signal_from_responder->functions->get(
            initiator->config->signal_from_responder->signal_in_handle);
    initiator->responder_signal_changed =
        (abn_sync_Atomic_ExchangeAcquire(
             initiator->config->handling_responder_signal_changed_event, 0) !=
         0);
    if (initiator->responder_signal_changed) {
      responder_signal_state =
          initiator->config->signal_from_responder->functions->get(
              initiator->config->signal_from_responder->signal_in_handle);

      // this is just a sanity check to avoid any Initiator signaling issue
      if (abn_sync_Atomic_ExchangeAcquire(
              initiator->config->handling_responder_signal_changed_event, 0) !=
          0) {
        ABN_LOG_ERROR(
            "More than two Responder signal transitions detected within a "
            "singular event");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }
    }

    if (responder_signal_state == initiator->last_responder_signal_state) {
      initiator->responder_signal_changed_to_active = true;
      initiator->responder_signal_changed_to_inactive = true;
    } else {
      if (responder_signal_state == ABN_SIGNAL_STATE_ACTIVE) {
        initiator->responder_signal_changed_to_active = true;
      } else {  // responder_signal_state == ABN_SIGNAL_STATE_INACTIVE
        initiator->responder_signal_changed_to_inactive = true;
      }
    }
    initiator->last_responder_signal_state = responder_signal_state;
    initiator->consecutive_responder_timeout_counter = 0;
  }

  return ABN_MAC_INITIATOR_EVENT_NONE;
}

static void abn_mac_Initiator_SetState(struct abn_mac_Initiator* initiator,
                                       enum abn_mac_Initiator_State state) {
  abn_mac_Pm_IdleCallback idle_callback;
  void* idle_callback_context;

  abn_sync_Atomic_StoreRelease(initiator->config->state, state);

  if (state == ABN_MAC_INITIATOR_STATE_IDLE) {
    if (initiator->idle_state_reported == ABN_MAC_PM_STATE_ENTER_IDLE) {
      return;
    }
  } else {
    if (initiator->idle_state_reported == ABN_MAC_PM_STATE_EXIT_IDLE) {
      return;
    }
  }

  abn_sync_Mutex_Lock(initiator->config->mutex_callback);
  idle_callback = initiator->idle_callback;
  idle_callback_context = initiator->idle_callback_context;
  abn_sync_Mutex_Unlock(initiator->config->mutex_callback);

  if (idle_callback == NULL) {
    return;
  }

  if (state == ABN_MAC_INITIATOR_STATE_IDLE) {
    if ((abn_sync_Atomic_LoadAcquire(
             initiator->config->handling_responder_signal_changed_event) !=
         0) ||
        (abn_sync_Atomic_LoadAcquire(initiator->config->transaction_result) !=
         ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE)) {
      return;
    }
    initiator->idle_state_reported = ABN_MAC_PM_STATE_ENTER_IDLE;
  } else {
    initiator->idle_state_reported = ABN_MAC_PM_STATE_EXIT_IDLE;
  }
  idle_callback(idle_callback_context, initiator->idle_state_reported);
}

static enum abn_mac_Initiator_Event abn_mac_Initiator_GetNextEvent(
    struct abn_mac_Initiator* initiator) {
  int transfer_result;
  bool timer_expired;
  enum abn_mac_Initiator_Event signal_next_event;

  signal_next_event = abn_mac_Initiator_ResponderSignalHandle(initiator);
  if (signal_next_event != ABN_MAC_INITIATOR_EVENT_NONE) {
    return signal_next_event;
  }

  transfer_result =
      abn_sync_Atomic_LoadAcquire(initiator->config->transaction_result);
  timer_expired =
      abn_sync_Atomic_LoadAcquire(initiator->config->timer_expired) != 0;

  switch (abn_sync_Atomic_LoadAcquire(initiator->config->state)) {
    case ABN_MAC_INITIATOR_STATE_IDLE:
      // Unexpected events.
      if (transfer_result != ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE) {
        ABN_LOG_ERROR("Unexpected transfer result received in idle: %d",
                      transfer_result);
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }
      if (timer_expired) {
        ABN_LOG_ERROR("Timer expired unexpectedly while in idle");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }
      if (initiator->responder_signal_changed_to_inactive) {
        ABN_LOG_ERROR("Responder signal unexpectedly lowered while in idle");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (initiator->responder_signal_changed_to_active) {
        return ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_WHILE_IN_IDLE;
      }
      if (initiator->data_client->functions->is_tx_data_available(
              initiator->data_client->client_handle)) {
        return ABN_MAC_INITIATOR_EVENT_TX_DATA_AVAILABLE;
      }

      return ABN_MAC_INITIATOR_EVENT_NONE;

    case ABN_MAC_INITIATOR_STATE_WAITING_FOR_RESPONDER_READY:
      // Unexpected events.
      if (transfer_result != ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE) {
        ABN_LOG_ERROR(
            "Unexpected transfer result received while handshake in "
            "progress: %d",
            transfer_result);
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }
      if (initiator->responder_signal_changed_to_inactive) {
        ABN_LOG_ERROR(
            "Responder signal unexpectedly inactive while waiting "
            "for responder signal active");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (timer_expired) {
        return ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_TIMER_EXPIRED;
      }
      if (initiator->responder_signal_changed_to_active) {
        return ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_WHILE_WAITING_FOR_RESPONDER_READY;
      }
      return ABN_MAC_INITIATOR_EVENT_NONE;

    case ABN_MAC_INITIATOR_STATE_WAITING_FOR_TRANSFER_DONE:
      // Unexpected events.
      // TODO: b/313717023 - Check if we handle correctly here the posibility
      // of responder to activate its signal after rebooting
      if (initiator->responder_signal_changed_to_active) {
        ABN_LOG_ERROR(
            "Responder signal unexpectedly active while transaction "
            "in progress");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }
      if (initiator->responder_signal_changed_to_inactive) {
        ABN_LOG_ERROR(
            "Responder signal unexpectedly inactive while transaction "
            "in progress");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (timer_expired) {
        return ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_TIMER_EXPIRED;
      }

      if (transfer_result == ABN_MAC_INITIATOR_TRANSPORT_RESULT_ERROR) {
        return ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_ERROR_WHILE_WAITING_FOR_TRANSFER_DONE;
      }
      if (transfer_result == ABN_MAC_INITIATOR_TRANSPORT_RESULT_SUCCESS) {
        return ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_SUCCESS_WHILE_WAITING_FOR_TRANSFER_DONE;
      }

      return ABN_MAC_INITIATOR_EVENT_NONE;

    case ABN_MAC_INITIATOR_STATE_WAITING_FOR_RESPONDER_DONE:
      // Unexpected events.
      if (transfer_result != ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE) {
        ABN_LOG_ERROR(
            "Unexpected transfer result received post "
            "transaction: %d",
            transfer_result);
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }
      if (initiator->responder_signal_changed_to_active) {
        ABN_LOG_ERROR(
            "Responder signal unexpectedly active while waiting "
            "for responder signal inactive");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (timer_expired) {
        return ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_TIMER_EXPIRED;
      }
      if (initiator->responder_signal_changed_to_inactive) {
        return ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_WHILE_WAITING_FOR_RESPONDER_DONE;
      }
      return ABN_MAC_INITIATOR_EVENT_NONE;

    case ABN_MAC_INITIATOR_STATE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE:
      // Unexpected events.
      // TODO: b/313717023 - Check if we handle correctly here the posibility
      // of responder to activate its signal after rebooting
      if (initiator->responder_signal_changed_to_active) {
        ABN_LOG_ERROR(
            "Responder signal unexpectedly active while waiting "
            "for transfer done and responder inactive");
        return ABN_MAC_INITIATOR_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (timer_expired) {
        return ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_TIMER_EXPIRED;
      }

      if (initiator->responder_signal_changed_to_inactive) {
        if (transfer_result == ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE) {
          return ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE;
        }
        if (transfer_result == ABN_MAC_INITIATOR_TRANSPORT_RESULT_ERROR) {
          return ABN_MAC_INITIATOR_EVENT_TRANSFER_ERROR_AND_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE;
        }
        if (transfer_result == ABN_MAC_INITIATOR_TRANSPORT_RESULT_SUCCESS) {
          return ABN_MAC_INITIATOR_EVENT_TRANSFER_SUCCESS_AND_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE;
        }
      }

      if (transfer_result == ABN_MAC_INITIATOR_TRANSPORT_RESULT_ERROR) {
        return ABN_MAC_INITIATOR_EVENT_TRANSFER_ERROR_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE;
      }
      if (transfer_result == ABN_MAC_INITIATOR_TRANSPORT_RESULT_SUCCESS) {
        return ABN_MAC_INITIATOR_EVENT_TRANSFER_SUCCESS_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE;
      }

      return ABN_MAC_INITIATOR_EVENT_NONE;

    case ABN_MAC_INITIATOR_STATE_UNINITIALIZED:
      ABN_ASSERT(false);
      return ABN_MAC_INITIATOR_EVENT_NONE;
  }

  return ABN_MAC_INITIATOR_EVENT_NONE;
}

static void abn_mac_Initiator_PrepareTransaction(
    struct abn_mac_Initiator* initiator) {
  // TODO: b/313660370 - Since we prepare the buffers before getting the signal
  // from Responder, we should not call prepare_buffers() again if the Responder
  // times out
  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_PREPARE_BUFFERS_START,
      /*state=*/0,
      /*flags=*/0);
  initiator->data_client->functions->prepare_buffers(
      initiator->data_client->client_handle,
      &initiator->tx_data,
      &initiator->rx_data);
  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_PREPARE_BUFFERS_END,
      /*state=*/0,
      /*flags=*/0);
  initiator->client_transaction_pending = true;

  initiator->config->transport->functions->prepare_transfer(
      initiator->config->transport->transport_handle,
      (uint8_t*)initiator->tx_data,
      (uint8_t*)initiator->rx_data,
      ABN_FRAME_SIZE);
}

static void abn_mac_Initiator_ProcessEventResponderReadyWhileInIdle(
    struct abn_mac_Initiator* initiator) {
  initiator->config->timer->functions->cancel(
      initiator->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(initiator->config->timer_expired, 0);

  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_SIGNAL_ACTIVE_FROM_RESPONDER,
      /*state=*/0,
      /*flags=*/0);

  abn_mac_Initiator_PrepareTransaction(initiator);

  // Note we may have already raised the signal in case we have data to send.
  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_ACTIVE);

  initiator->config->timer->functions->invoke_after(
      initiator->config->timer->timer_handle,
      initiator->config->timeout_transaction_done);
  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_INITIATE_TRANSFER,
      /*state=*/0,
      /*flags=*/0);

  initiator->config->transport->functions->initiate_transfer(
      initiator->config->transport->transport_handle);

  abn_mac_Initiator_SetState(
      initiator,
      ABN_MAC_INITIATOR_STATE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE);
}

static void
abn_mac_Initiator_ProcessEventResponderReadyWhileWaitingForResponderReady(
    struct abn_mac_Initiator* initiator) {
  initiator->config->timer->functions->cancel(
      initiator->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(initiator->config->timer_expired, 0);

  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_SIGNAL_ACTIVE_FROM_RESPONDER,
      /*state=*/0,
      /*flags=*/0);

  // No need to call PrepareTransaction() since
  // it was already called, based on the state machine

  // Note we may have already raised the signal in case we have data to send.

  initiator->config->timer->functions->invoke_after(
      initiator->config->timer->timer_handle,
      initiator->config->timeout_transaction_done);
  initiator->config->transport->functions->initiate_transfer(
      initiator->config->transport->transport_handle);

  abn_mac_Initiator_SetState(
      initiator,
      ABN_MAC_INITIATOR_STATE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE);
}

static void abn_mac_Initiator_ProcessEventTxDataAvailable(
    struct abn_mac_Initiator* initiator) {
  abn_mac_Initiator_PrepareTransaction(initiator);

  initiator->config->timer->functions->invoke_after(
      initiator->config->timer->timer_handle,
      initiator->config->timeout_responder_ready);
  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_ACTIVE);

  abn_mac_Initiator_SetState(
      initiator, ABN_MAC_INITIATOR_STATE_WAITING_FOR_RESPONDER_READY);
}

static void abn_mac_Initiator_ProcessEventTransferResult(
    struct abn_mac_Initiator* initiator, bool success) {
  abn_sync_Atomic_StoreRelaxed(initiator->config->transaction_result,
                               ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE);
  if (success) {
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_PROCESS_BUFFERS_START,
        /*state=*/0,
        /*flags=*/0);
    initiator->data_client->functions->process_rx_data(
        initiator->data_client->client_handle, initiator->rx_data);
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_PROCESS_BUFFERS_END,
        /*state=*/0,
        /*flags=*/0);
    initiator->client_transaction_pending = false;
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_TRANSACTION_SUCCESS,
        /*state=*/0,
        /*flags=*/0);
  } else {
    initiator->data_client->functions->handle_transaction_failure(
        initiator->data_client->client_handle);
    initiator->client_transaction_pending = false;
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_TRANSACTION_ERROR,
        abn_sync_Atomic_LoadAcquire(initiator->config->state),
        /*flags=*/0);
  }

  initiator->config->timer->functions->cancel(
      initiator->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(initiator->config->timer_expired, 0);

  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  abn_mac_Initiator_SetState(initiator, ABN_MAC_INITIATOR_STATE_IDLE);
}

static void
abn_mac_Initiator_ProcessEventResponderDoneWhileWaitingForResponderDone(
    struct abn_mac_Initiator* initiator) {
  // There is a corner case here, where here it might be a race condition and
  // the signal is activated again right before we clear
  // responder_signal_changed (because Responder timed out. goes to IDLE
  // and wants to start a transaction again). We handle that correctly:
  // Responder is going to timeout waiting for a transaction, goes to IDLE and
  // activate again its signal

  initiator->config->timer->functions->cancel(
      initiator->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(initiator->config->timer_expired, 0);
  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_SIGNAL_INACTIVE_FROM_RESPONDER,
      /*state=*/0,
      /*flags=*/0);

  abn_mac_Initiator_SetState(initiator, ABN_MAC_INITIATOR_STATE_IDLE);
}

static void abn_mac_Initiator_ProcessEventClearPendingVariables(
    struct abn_mac_Initiator* initiator) {
  abn_sync_Atomic_StoreRelaxed(initiator->config->transaction_result,
                               ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE);
  abn_sync_Atomic_StoreRelaxed(initiator->config->timer_expired, 0);

  initiator->client_transaction_pending = false;
  initiator->responder_signal_changed_to_active = false;
  initiator->responder_signal_changed_to_inactive = false;

  initiator->responder_signal_changed = false;

  initiator->last_responder_signal_state = ABN_SIGNAL_STATE_UNKNOWN;
}

// TODO: b/313658637 - implement a more complex invalid state transition handler
// eventually using a timer with an extra state to avoid the races
static void abn_mac_Initiator_ProcessEventInvalidState(
    struct abn_mac_Initiator* initiator) {
  uint32_t flags = 0;

  initiator->config->timer->functions->cancel(
      initiator->config->timer->timer_handle);
  initiator->config->transport->functions->abort_transfer(
      initiator->config->transport->transport_handle);
  if (initiator->client_transaction_pending) {
    initiator->data_client->functions->handle_transaction_failure(
        initiator->data_client->client_handle);
    initiator->client_transaction_pending = false;
  }

  if (initiator->responder_signal_changed_to_active) {
    flags |= ABN_MAC_INITIATOR_PENDING_EVENT_RESPONDER_READY;
  }
  if (initiator->responder_signal_changed_to_inactive) {
    flags |= ABN_MAC_INITIATOR_PENDING_EVENT_RESPONDER_DONE;
  }

  // Note the atomic signals are cleared below in
  // ProcessEventClearPendingVariables.
  if (abn_sync_Atomic_LoadAcquire(initiator->config->transaction_result) != 0) {
    flags |= ABN_MAC_INITIATOR_PENDING_EVENT_TRANSFER_RESULT;
  }
  if (abn_sync_Atomic_LoadAcquire(initiator->config->timer_expired) != 0) {
    flags |= ABN_MAC_INITIATOR_PENDING_EVENT_TIMER_EXPIRED;
  }

  abn_mac_Initiator_ProcessEventClearPendingVariables(initiator);

  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_INVALID_STATE,
      abn_sync_Atomic_LoadAcquire(initiator->config->state),
      flags);

  abn_mac_Initiator_SetState(initiator, ABN_MAC_INITIATOR_STATE_IDLE);
}

// This might happen because the transport result callback hasn't been received
// or the Responder signal deactivation hasn't been detected.
static void abn_mac_Initiator_ProcessEventTransferResultTimerExpired(
    struct abn_mac_Initiator* initiator) {
  abn_sync_Atomic_StoreRelaxed(initiator->config->timer_expired, 0);

  initiator->config->transport->functions->abort_transfer(
      initiator->config->transport->transport_handle);
  abn_sync_Atomic_StoreRelaxed(initiator->config->transaction_result,
                               ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE);

  initiator->data_client->functions->handle_transaction_failure(
      initiator->data_client->client_handle);
  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_TIMER_EXPIRED,
      abn_sync_Atomic_LoadAcquire(initiator->config->state),
      /*flags=*/0);

  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  abn_mac_Initiator_SetState(initiator, ABN_MAC_INITIATOR_STATE_IDLE);
}

static void abn_mac_Initiator_ProcessEventResponderReadyTimerExpired(
    struct abn_mac_Initiator* initiator) {
  abn_mac_Initiator_ProcessEventClearPendingVariables(initiator);

  initiator->config->transport->functions->abort_transfer(
      initiator->config->transport->transport_handle);
  initiator->data_client->functions->handle_transaction_failure(
      initiator->data_client->client_handle);
  // Note that the transfer has not been initiated ergo the signal does
  // not have to be cleared as it could not have fired during abort.

  // Signal inactive here, in case we missed the edge from Responder
  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_TIMER_EXPIRED,
      abn_sync_Atomic_LoadAcquire(initiator->config->state),
      /*flags=*/0);

  abn_mac_Initiator_SetState(initiator, ABN_MAC_INITIATOR_STATE_IDLE);
  ++initiator->consecutive_responder_timeout_counter;
  if (initiator->consecutive_responder_timeout_counter >=
      initiator->config->max_consecutive_responder_timeout_retries) {
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_RESPONDER_NOT_RESPONDING,
        /*state=*/0,
        /*flags=*/0);
    initiator->consecutive_responder_timeout_counter = 0;
  }
}

static void abn_mac_Initiator_ProcessEventResponderDoneTimerExpired(
    struct abn_mac_Initiator* initiator) {
  abn_mac_Initiator_ProcessEventClearPendingVariables(initiator);

  initiator->config->signal_to_responder->functions->set(
      initiator->config->signal_to_responder->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  initiator->config->client_callback(
      initiator->config->client_callback_context,
      ABN_MAC_INITIATOR_CALLBACK_OPCODE_TIMER_EXPIRED,
      abn_sync_Atomic_LoadAcquire(initiator->config->state),
      /*flags=*/0);

  abn_mac_Initiator_SetState(initiator, ABN_MAC_INITIATOR_STATE_IDLE);
}

static void
abn_mac_Initiator_ProcessEventTransferResultWhileWaitingForTransferDoneAndResponderDone(
    struct abn_mac_Initiator* initiator, bool success) {
  abn_sync_Atomic_StoreRelaxed(initiator->config->transaction_result,
                               ABN_MAC_INITIATOR_TRANSPORT_RESULT_NONE);
  if (success) {
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_PROCESS_BUFFERS_START,
        /*state=*/0,
        /*flags=*/0);
    initiator->data_client->functions->process_rx_data(
        initiator->data_client->client_handle, initiator->rx_data);
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_PROCESS_BUFFERS_END,
        /*state=*/0,
        /*flags=*/0);
    initiator->client_transaction_pending = false;
  } else {
    initiator->data_client->functions->handle_transaction_failure(
        initiator->data_client->client_handle);
    initiator->client_transaction_pending = false;
    initiator->config->client_callback(
        initiator->config->client_callback_context,
        ABN_MAC_INITIATOR_CALLBACK_OPCODE_TRANSACTION_ERROR,
        abn_sync_Atomic_LoadAcquire(initiator->config->state),
        /*flags=*/0);
  }

  abn_mac_Initiator_SetState(
      initiator, ABN_MAC_INITIATOR_STATE_WAITING_FOR_RESPONDER_DONE);
}

static void abn_mac_Initiator_ProcessEvent(struct abn_mac_Initiator* initiator,
                                           enum abn_mac_Initiator_Event event) {
  abn_sync_Atomic_StoreRelease(initiator->config->processing_event, 1);

  switch (event) {
    case ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_WHILE_IN_IDLE:
      abn_mac_Initiator_ProcessEventResponderReadyWhileInIdle(initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_WHILE_WAITING_FOR_RESPONDER_READY:
      abn_mac_Initiator_ProcessEventResponderReadyWhileWaitingForResponderReady(
          initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_WHILE_WAITING_FOR_RESPONDER_DONE:
      abn_mac_Initiator_ProcessEventResponderDoneWhileWaitingForResponderDone(
          initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_TX_DATA_AVAILABLE:
      abn_mac_Initiator_ProcessEventTxDataAvailable(initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_SUCCESS_WHILE_WAITING_FOR_TRANSFER_DONE:
      abn_mac_Initiator_ProcessEventTransferResult(initiator, true);
      break;
    case ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_ERROR_WHILE_WAITING_FOR_TRANSFER_DONE:
      abn_mac_Initiator_ProcessEventTransferResult(initiator, false);
      break;
    case ABN_MAC_INITIATOR_EVENT_INVALID_STATE:
      abn_mac_Initiator_ProcessEventInvalidState(initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_TRANSFER_RESULT_TIMER_EXPIRED:
      abn_mac_Initiator_ProcessEventTransferResultTimerExpired(initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_RESPONDER_READY_TIMER_EXPIRED:
      abn_mac_Initiator_ProcessEventResponderReadyTimerExpired(initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_TIMER_EXPIRED:
      abn_mac_Initiator_ProcessEventResponderDoneTimerExpired(initiator);
      break;
    case ABN_MAC_INITIATOR_EVENT_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE:
      abn_mac_Initiator_SetState(
          initiator, ABN_MAC_INITIATOR_STATE_WAITING_FOR_TRANSFER_DONE);
      break;
    case ABN_MAC_INITIATOR_EVENT_TRANSFER_ERROR_AND_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE:
      abn_mac_Initiator_ProcessEventTransferResult(initiator, false);
      break;
    case ABN_MAC_INITIATOR_EVENT_TRANSFER_SUCCESS_AND_RESPONDER_DONE_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE:
      abn_mac_Initiator_ProcessEventTransferResult(initiator, true);
      break;
    case ABN_MAC_INITIATOR_EVENT_TRANSFER_ERROR_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE:
      abn_mac_Initiator_ProcessEventTransferResultWhileWaitingForTransferDoneAndResponderDone(
          initiator, false);
      break;
    case ABN_MAC_INITIATOR_EVENT_TRANSFER_SUCCESS_WHILE_WAITING_FOR_TRANSFER_DONE_AND_RESPONDER_DONE:
      abn_mac_Initiator_ProcessEventTransferResultWhileWaitingForTransferDoneAndResponderDone(
          initiator, true);
      break;
    case ABN_MAC_INITIATOR_EVENT_NONE:
      break;
  }

  abn_sync_Atomic_StoreRelease(initiator->config->processing_event, 0);
}

// External APIs

bool abn_mac_Initiator_ProcessOneEvent(struct abn_mac_Initiator* initiator) {
  enum abn_mac_Initiator_Event event;
  ABN_ASSERT(abn_sync_Atomic_LoadAcquire(initiator->config->state) !=
             ABN_MAC_INITIATOR_STATE_UNINITIALIZED);

  event = abn_mac_Initiator_GetNextEvent(initiator);
  if (event == ABN_MAC_INITIATOR_EVENT_NONE) {
    return false;
  }
  abn_mac_Initiator_ProcessEvent(initiator, event);
  return true;
}

void abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent(
    struct abn_mac_Initiator* initiator) {
  ABN_ASSERT(initiator != NULL);

  // Process all events.
  while (abn_mac_Initiator_ProcessOneEvent(initiator)) {
  }

  // Wait for the next event.
  abn_sync_ThreadNotification_Acquire(initiator->config->event_notification);
}

void abn_mac_Initiator_Init(struct abn_mac_Initiator* initiator,
                            const struct abn_mac_Initiator_Config* config,
                            const struct abn_mac_Client* data_client) {
  ABN_ASSERT(initiator != NULL);

  initiator->config = config;
  initiator->data_client = data_client;

  initiator->config->signal_from_responder->functions->set_transition_handler(
      initiator->config->signal_from_responder->signal_in_handle,
      abn_mac_Initiator_ResponderSignalTransitionHandler,
      initiator);

  initiator->config->signal_from_responder->functions
      ->enable_transition_handler(
          initiator->config->signal_from_responder->signal_in_handle, true);

  initiator->config->transport->functions->set_result_handler(
      initiator->config->transport->transport_handle,
      abn_mac_Initiator_TransportResultHandler,
      initiator);

  initiator->config->timer->functions->set_expiry_handler(
      initiator->config->timer->timer_handle,
      abn_mac_Initiator_TimerExpiryHandler,
      initiator);

  abn_mac_Initiator_ProcessEventClearPendingVariables(initiator);

  abn_sync_Atomic_StoreRelaxed(
      initiator->config->handling_responder_signal_changed_event, 0);

  abn_sync_Atomic_StoreRelease(initiator->config->state,
                               ABN_MAC_INITIATOR_STATE_IDLE);

  initiator->idle_callback = NULL;
  initiator->idle_callback_context = NULL;

  initiator->client_transaction_pending = false;

  abn_sync_Atomic_StoreRelease(initiator->config->processing_event, 0);

  initiator->idle_state_reported = ABN_MAC_PM_STATE_UNKNOWN;

  initiator->consecutive_responder_timeout_counter = 0;
}

// TODO: b/313928599 - Disable all pending things: timer, transaction
// Disable callback handles
void abn_mac_Initiator_Deinit(struct abn_mac_Initiator* initiator) {
  ABN_ASSERT(initiator != NULL);

  abn_sync_Atomic_StoreRelease(initiator->config->state,
                               ABN_MAC_INITIATOR_STATE_UNINITIALIZED);
}

bool abn_mac_Initiator_IsIdle(void* context) {
  struct abn_mac_Initiator* initiator = (struct abn_mac_Initiator*)context;

  ABN_ASSERT(initiator != NULL);

  // By design, we need to handle the signal independently from Autobahn Core
  // As a result, all we need to do here is to check the state variable
  // this is safe, since state is Atomic
  return (
      (abn_sync_Atomic_LoadAcquire(initiator->config->state) ==
       ABN_MAC_INITIATOR_STATE_IDLE) &&
      (abn_sync_Atomic_LoadAcquire(initiator->config->processing_event) == 0));
}

void abn_mac_Initiator_SetIdleCallback(void* container_handle,
                                       abn_mac_Pm_IdleCallback callback,
                                       void* context) {
  struct abn_mac_Initiator* initiator =
      (struct abn_mac_Initiator*)container_handle;

  ABN_ASSERT(initiator != NULL);

  abn_sync_Mutex_Lock(initiator->config->mutex_callback);
  initiator->idle_callback = callback;
  initiator->idle_callback_context = context;
  abn_sync_Mutex_Unlock(initiator->config->mutex_callback);
}
