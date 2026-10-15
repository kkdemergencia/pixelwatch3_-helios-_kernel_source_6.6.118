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

#include "autobahn/mac/responder.h"

#include "autobahn/assert/assert.h"
#include "autobahn/autobahn.h"
#include "autobahn/log/log.h"
#include "autobahn/types.h"

static void abn_Responder_InitiatorSignalTransitionHandle(void* context) {
  struct abn_mac_Responder* responder = (struct abn_mac_Responder*)context;

  ABN_ASSERT(responder != NULL);

  abn_sync_Atomic_StoreRelease(responder->config->initiator_signal_transitioned,
                               1);

  // Notify the MAC thread
  abn_sync_ThreadNotification_Release(responder->config->event_notification);
}

static void abn_mac_Responder_TransportResultHandle(void* context,
                                                    bool success) {
  struct abn_mac_Responder* responder = (struct abn_mac_Responder*)context;

  ABN_ASSERT(responder != NULL);

  if (success) {
    abn_sync_Atomic_StoreRelease(responder->config->transfer_result,
                                 ABN_MAC_RESPONDER_TRANSFER_RESULT_SUCCESS);
  } else {
    abn_sync_Atomic_StoreRelease(responder->config->transfer_result,
                                 ABN_MAC_RESPONDER_TRANSFER_RESULT_ERROR);
  }

  // Notify the MAC thread
  abn_sync_ThreadNotification_Release(responder->config->event_notification);
}

static void abn_mac_Responder_TimerExpiryHandler(void* context) {
  struct abn_mac_Responder* responder = (struct abn_mac_Responder*)context;

  ABN_ASSERT(responder != NULL);

  abn_sync_Atomic_StoreRelease(responder->config->timer_expired, 1);

  // Notify the MAC thread
  abn_sync_ThreadNotification_Release(responder->config->event_notification);
}

static void abn_mac_Responder_SetState(struct abn_mac_Responder* responder,
                                       enum abn_mac_Responder_State state) {
  abn_sync_Atomic_StoreRelease(responder->config->state, state);

  if (state == ABN_MAC_RESPONDER_STATE_IDLE) {
    if (responder->idle_state_reported == ABN_MAC_PM_STATE_ENTER_IDLE) {
      return;
    }
  } else {
    if (responder->idle_state_reported == ABN_MAC_PM_STATE_EXIT_IDLE) {
      return;
    }
  }

  if (responder->idle_callback == NULL) {
    return;
  }

  if (state == ABN_MAC_RESPONDER_STATE_IDLE) {
    if ((abn_sync_Atomic_LoadAcquire(
             responder->config->initiator_signal_transitioned) != 0) ||
        (abn_sync_Atomic_LoadAcquire(responder->config->transfer_result) !=
         ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE)) {
      return;
    }
    responder->idle_state_reported = ABN_MAC_PM_STATE_ENTER_IDLE;
  } else {
    responder->idle_state_reported = ABN_MAC_PM_STATE_EXIT_IDLE;
  }
  responder->idle_callback(responder->idle_callback_context,
                           responder->idle_state_reported);
}

// TODO: b/314338269 - Check if we handle correctly here the posibility
// of Initiator to reboot in all transition states
static enum abn_mac_Responder_Event abn_mac_Responder_GetNextEvent(
    struct abn_mac_Responder* responder) {
  enum abn_Signal_State initiator_signal_state;
  bool initiator_signal_transitioned;

  responder->initiator_signal_transitioned_and_is_now_active = false;
  responder->initiator_signal_transitioned_and_is_now_inactive = false;

  initiator_signal_transitioned =
      (abn_sync_Atomic_ExchangeAcquire(
           responder->config->initiator_signal_transitioned, 0) != 0);
  if (initiator_signal_transitioned) {
    initiator_signal_state =
        responder->config->signal_from_initiator->functions->get(
            responder->config->signal_from_initiator->signal_in_handle);

    // There's a risk that the interrupt fires between the atomic exchange
    // of initiator_signal_transitioned and reading the signal state, as this
    // can be preempted by other threads and interrupts including the signal
    // interrupt itself. Without re-checking the signal we may accidentally
    // leave initiator_signal_transitioned set to true even though the final
    // signal state has already been consumed, meaning a false pulse would be
    // reported on the next cycle.
    //
    // Instead the signal and the atomic exchange are done multiple times until
    // they are consistent across the exchange. A coalesced pulse which is
    // expected in a healthy case may require a single re-read. If more than
    // one re-read is required it's possible that either the other side aborted
    // or the other side is in an even worse state. Instead of having an
    // unbounded loop we simply error out if it takes more than one retry.

    initiator_signal_transitioned =
        (abn_sync_Atomic_ExchangeAcquire(
             responder->config->initiator_signal_transitioned, 0) != 0);
    if (initiator_signal_transitioned) {
      initiator_signal_state =
          responder->config->signal_from_initiator->functions->get(
              responder->config->signal_from_initiator->signal_in_handle);

      // this is just a sanity check to avoid any Initiator signaling issue
      if (abn_sync_Atomic_ExchangeAcquire(
              responder->config->initiator_signal_transitioned, 0) != 0) {
        ABN_LOG_ERROR("More than two Initiator signal transitions detected");
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }
    }

    if (initiator_signal_state == responder->last_initiator_signal_state) {
      responder->initiator_signal_transitioned_and_is_now_active = true;
      responder->initiator_signal_transitioned_and_is_now_inactive = true;
    } else {
      if (initiator_signal_state == ABN_SIGNAL_STATE_ACTIVE) {
        responder->initiator_signal_transitioned_and_is_now_active = true;
      } else {  // initiator_signal_state == ABN_SIGNAL_STATE_INACTIVE
        responder->initiator_signal_transitioned_and_is_now_inactive = true;
      }
    }
    responder->last_initiator_signal_state = initiator_signal_state;
  }
  const int transfer_result =
      abn_sync_Atomic_LoadAcquire(responder->config->transfer_result);
  const int timer_expired =
      abn_sync_Atomic_LoadAcquire(responder->config->timer_expired);

  switch (abn_sync_Atomic_LoadAcquire(responder->config->state)) {
    case ABN_MAC_RESPONDER_STATE_IDLE:
      // Unexpected events.
      if (transfer_result != ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE) {
        ABN_LOG_ERROR("Unexpected transfer result received in idle: %d",
                      transfer_result);
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }
      if (timer_expired) {
        ABN_LOG_ERROR("Timer expired while in idle");
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }
      if (responder->initiator_signal_transitioned_and_is_now_inactive) {
        ABN_LOG_DEBUG(
            "Initiator signal unexpectedly deactivated while in idle");
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (responder->initiator_signal_transitioned_and_is_now_active) {
        return ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_IDLE;
      }
      if (responder->data_client->functions->is_tx_data_available(
              responder->data_client->client_handle)) {
        return ABN_MAC_RESPONDER_EVENT_TX_DATA_AVAILABLE;
      }

      return ABN_MAC_RESPONDER_EVENT_NONE;

    case ABN_MAC_RESPONDER_STATE_TRANSFER_IN_PROGRESS:
      // Unexpected events.
      if (responder->initiator_signal_transitioned_and_is_now_inactive) {
        ABN_LOG_ERROR(
            "Initiator signal unexpectedly deactivated while transaction is "
            "pending");
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }
      if (responder->initiator_signal_transitioned_and_is_now_active) {
        ABN_LOG_ERROR(
            "Initiator signal unexpectedly activated while transaction is "
            "pending");
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (timer_expired) {
        return ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_TIMER_EXPIRED;
      }
      if (transfer_result == ABN_MAC_RESPONDER_TRANSFER_RESULT_SUCCESS) {
        return ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_SUCCESS;
      }
      if (transfer_result == ABN_MAC_RESPONDER_TRANSFER_RESULT_ERROR) {
        return ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_ERROR;
      }

      return ABN_MAC_RESPONDER_EVENT_NONE;

    case ABN_MAC_RESPONDER_STATE_WAITING_FOR_INITIATOR_READY:
      // Unexpected events.
      if (responder->initiator_signal_transitioned_and_is_now_inactive) {
        ABN_LOG_ERROR(
            "Initiator signal unexpectedly deactivated while "
            "waiting to be ready");
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }
      if (transfer_result != ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE) {
        ABN_LOG_ERROR(
            "Unexpected transfer result received while waiting "
            "for Initiator to be ready: %d",
            (int)transfer_result);
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }

      // Expected events.
      if (timer_expired) {
        return ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_TIMER_EXPIRED;
      }

      if (responder->initiator_signal_transitioned_and_is_now_active) {
        return ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_WAITING;
      }
      return ABN_MAC_RESPONDER_EVENT_NONE;

    case ABN_MAC_RESPONDER_STATE_WAITING_FOR_INITIATOR_DONE:
      // Unexpected events.
      if (transfer_result != ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE) {
        ABN_LOG_ERROR(
            "Unexpected transfer result received in waiting "
            "for initiator signal to deactivate: %d",
            transfer_result);
        return ABN_MAC_RESPONDER_EVENT_INVALID_STATE;
      }
      // Expected events.
      if (timer_expired) {
        return ABN_MAC_RESPONDER_EVENT_INITIATOR_DONE_TIMER_EXPIRED;
      }
      if (responder->initiator_signal_transitioned_and_is_now_active) {
        return ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_WAITING_FOR_INITIATOR_DONE;
      }
      if (responder->initiator_signal_transitioned_and_is_now_inactive) {
        return ABN_MAC_RESPONDER_EVENT_INITIATOR_DONE;
      }
      return ABN_MAC_RESPONDER_EVENT_NONE;

    case ABN_MAC_RESPONDER_STATE_UNINITIALIZED:
      ABN_ASSERT(false);
  }
  ABN_ASSERT(false);  // Unreachable
}

static void abn_mac_Responder_SetSignalActive(
    struct abn_mac_Responder* responder) {
  responder->config->signal_to_initiator->functions->set(
      responder->config->signal_to_initiator->signal_out_handle,
      ABN_SIGNAL_STATE_ACTIVE);

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_SIGNAL_ACTIVE_TO_INITIATOR,
      0,
      0);
}

static void abn_mac_Responder_ProcessEventTransferResult(
    struct abn_mac_Responder* responder, bool success) {
  abn_sync_Atomic_StoreRelaxed(responder->config->transfer_result,
                               ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE);

  responder->config->timer->functions->cancel(
      responder->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  if (success) {
    responder->config->client_callback(
        responder->config->client_callback_context,
        ABN_MAC_RESPONDER_CALLBACK_OPCODE_PROCESS_BUFFERS_START,
        0,
        0);
    responder->data_client->functions->process_rx_data(
        responder->data_client->client_handle, responder->rx_data);
    responder->config->client_callback(
        responder->config->client_callback_context,
        ABN_MAC_RESPONDER_CALLBACK_OPCODE_PROCESS_BUFFERS_END,
        0,
        0);

    responder->client_transaction_pending = false;
    responder->config->client_callback(
        responder->config->client_callback_context,
        ABN_MAC_RESPONDER_CALLBACK_OPCODE_TRANSFER_SUCCESS,
        abn_sync_Atomic_LoadAcquire(responder->config->state),
        0);
  } else {
    ABN_LOG_ERROR("Transfer error");
    responder->data_client->functions->handle_transaction_failure(
        responder->data_client->client_handle);
    responder->config->client_callback(
        responder->config->client_callback_context,
        ABN_MAC_RESPONDER_CALLBACK_OPCODE_TRANSFER_ERROR,
        abn_sync_Atomic_LoadAcquire(responder->config->state),
        0);
  }

  responder->config->signal_to_initiator->functions->set(
      responder->config->signal_to_initiator->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_SIGNAL_INACTIVE_TO_INITIATOR,
      0,
      0);

  responder->config->timer->functions->invoke_after(
      responder->config->timer->timer_handle,
      responder->config->timeout_initiator_done);

  abn_mac_Responder_SetState(
      responder, ABN_MAC_RESPONDER_STATE_WAITING_FOR_INITIATOR_DONE);
}

static void abn_mac_ResponderPrepareBuffersAndTransfer(
    struct abn_mac_Responder* responder) {
  // TODO: b/314334627 - We should not call prepare_buffers() again if
  // the transaction timeout
  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_PREPARE_BUFFERS_START,
      0,
      0);
  responder->data_client->functions->prepare_buffers(
      responder->data_client->client_handle,
      &responder->tx_data,
      &responder->rx_data);
  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_PREPARE_BUFFERS_END,
      0,
      0);

  responder->client_transaction_pending = true;

  responder->config->transport->functions->prepare_transfer(
      responder->config->transport->transport_handle,
      (uint8_t*)responder->tx_data,
      (uint8_t*)responder->rx_data,
      ABN_FRAME_SIZE);
}

static void abn_mac_Responder_ProcessEventTxDataAvailable(
    struct abn_mac_Responder* responder) {
  abn_mac_ResponderPrepareBuffersAndTransfer(responder);

  abn_mac_Responder_SetState(
      responder, ABN_MAC_RESPONDER_STATE_WAITING_FOR_INITIATOR_READY);

  abn_mac_Responder_SetSignalActive(responder);

  responder->config->timer->functions->invoke_after(
      responder->config->timer->timer_handle,
      responder->config->timeout_initiator_ready);
}

// This is called while Responder is in IDLE
// In this case, we need to call PrepareBuffersAndTransfer()
static void abn_mac_Responder_ProcessEventInitiatorReadyInIdle(
    struct abn_mac_Responder* responder) {
  abn_mac_ResponderPrepareBuffersAndTransfer(responder);

  responder->config->transport->functions->initiate_transfer(
      responder->config->transport->transport_handle);

  abn_mac_Responder_SetSignalActive(responder);

  responder->config->timer->functions->invoke_after(
      responder->config->timer->timer_handle,
      responder->config->timeout_transfer_result);

  abn_mac_Responder_SetState(responder,
                             ABN_MAC_RESPONDER_STATE_TRANSFER_IN_PROGRESS);
}

// This is called after Responder already activated its signal
// In this case, we already called PrepareBuffersAndTransfer()
static void abn_mac_Responder_ProcessEventInitiatorReadyInWaiting(
    struct abn_mac_Responder* responder) {
  responder->config->timer->functions->cancel(
      responder->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  responder->config->transport->functions->initiate_transfer(
      responder->config->transport->transport_handle);

  responder->config->timer->functions->invoke_after(
      responder->config->timer->timer_handle,
      responder->config->timeout_transfer_result);

  abn_mac_Responder_SetState(responder,
                             ABN_MAC_RESPONDER_STATE_TRANSFER_IN_PROGRESS);
}

static void abn_mac_Responder_ProcessEventInitiatorDone(
    struct abn_mac_Responder* responder) {
  responder->config->timer->functions->cancel(
      responder->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_SIGNAL_INACTIVE_FROM_INITIATOR,
      0,
      0);

  abn_mac_Responder_SetState(responder, ABN_MAC_RESPONDER_STATE_IDLE);
}

static void abn_mac_Responder_ClearPendingVariables(
    struct abn_mac_Responder* responder) {
  abn_sync_Atomic_StoreRelaxed(responder->config->transfer_result,
                               ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE);
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  responder->last_initiator_signal_state = ABN_SIGNAL_STATE_INACTIVE;

  responder->initiator_signal_transitioned_and_is_now_active = false;
  responder->initiator_signal_transitioned_and_is_now_inactive = false;

  responder->config->signal_to_initiator->functions->set(
      responder->config->signal_to_initiator->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);
}

// TODO: b/314336312 - implement a more complex invalid state transition handler
// eventually using a timer with an extra state to avoid the races
static void abn_mac_Responder_ProcessEventInvalidState(
    struct abn_mac_Responder* responder) {
  uint32_t flags = 0;

  responder->config->timer->functions->cancel(
      responder->config->timer->timer_handle);
  responder->config->transport->functions->abort_transfer(
      responder->config->transport->transport_handle);
  if (responder->client_transaction_pending) {
    responder->data_client->functions->handle_transaction_failure(
        responder->data_client->client_handle);
    responder->client_transaction_pending = false;
  }
  // Note the atomic signals are cleared below in
  // abn_mac_Responder_ClearPendingVariables.

  if (abn_sync_Atomic_LoadAcquire(responder->config->transfer_result) !=
      ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE) {
    flags |= ABN_MAC_RESPONDER_PENDING_EVENT_TRANSFER_RESULT;
  }

  if (responder->initiator_signal_transitioned_and_is_now_active) {
    flags |= ABN_MAC_RESPONDER_PENDING_EVENT_INITIATOR_READY;
  }
  if (responder->initiator_signal_transitioned_and_is_now_inactive) {
    flags |= ABN_MAC_RESPONDER_PENDING_EVENT_INITIATOR_DONE;
  }

  if (abn_sync_Atomic_LoadAcquire(responder->config->timer_expired)) {
    flags |= ABN_MAC_RESPONDER_PENDING_EVENT_TIMER_EXPIRED;
  }

  abn_mac_Responder_ClearPendingVariables(responder);

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_INVALID_STATE,
      abn_sync_Atomic_LoadAcquire(responder->config->state),
      flags);

  abn_mac_Responder_SetState(responder, ABN_MAC_RESPONDER_STATE_IDLE);

  responder->client_transaction_pending = false;
}

// TODO: b/314334627 - We should (or shouldn't?) call prepare_buffers() again if
// the transfer result timeout
static void abn_mac_Responder_ProcessEventTransferResultTimerExpired(
    struct abn_mac_Responder* responder) {
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  responder->config->transport->functions->abort_transfer(
      responder->config->transport->transport_handle);
  abn_sync_Atomic_StoreRelaxed(responder->config->transfer_result,
                               ABN_MAC_RESPONDER_TRANSFER_RESULT_NONE);

  responder->data_client->functions->handle_transaction_failure(
      responder->data_client->client_handle);

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_TIMER_EXPIRED,
      abn_sync_Atomic_LoadAcquire(responder->config->state),
      0);

  responder->config->signal_to_initiator->functions->set(
      responder->config->signal_to_initiator->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  responder->config->timer->functions->invoke_after(
      responder->config->timer->timer_handle,
      responder->config->timeout_initiator_done);

  abn_mac_Responder_SetState(
      responder, ABN_MAC_RESPONDER_STATE_WAITING_FOR_INITIATOR_DONE);
}

// TODO: b/314334627 - We should not call prepare_buffers() again if
// the Initiator signal ready timeout
static void abn_mac_Responder_ProcessEventInitiatorReadyTimerExpired(
    struct abn_mac_Responder* responder) {
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  responder->data_client->functions->handle_transaction_failure(
      responder->data_client->client_handle);

  responder->config->transport->functions->abort_transfer(
      responder->config->transport->transport_handle);
  // Note that the transfer has not been initiated ergo the signal does
  // not have to be cleared as it could not have fired during abort.

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_TIMER_EXPIRED,
      abn_sync_Atomic_LoadAcquire(responder->config->state),
      0);

  responder->config->signal_to_initiator->functions->set(
      responder->config->signal_to_initiator->signal_out_handle,
      ABN_SIGNAL_STATE_INACTIVE);

  abn_mac_Responder_SetState(responder, ABN_MAC_RESPONDER_STATE_IDLE);
}

static void
abn_mac_ResponderProcessEventInitiatorActiveWhileWaitingForInitiatorDone(
    struct abn_mac_Responder* responder) {
  // This is the case when Initiator signal goes inactive then goes active
  // and one of the transitions is missed
  responder->config->timer->functions->cancel(
      responder->config->timer->timer_handle);
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_INITIATOR_SIGNAL_INACTIVE_MISSED,
      abn_sync_Atomic_LoadAcquire(responder->config->state),
      0);

  abn_mac_ResponderPrepareBuffersAndTransfer(responder);

  responder->config->transport->functions->initiate_transfer(
      responder->config->transport->transport_handle);

  abn_mac_Responder_SetSignalActive(responder);

  responder->config->timer->functions->invoke_after(
      responder->config->timer->timer_handle,
      responder->config->timeout_transfer_result);

  abn_mac_Responder_SetState(responder,
                             ABN_MAC_RESPONDER_STATE_TRANSFER_IN_PROGRESS);
}

static void abn_mac_Responder_ProcessEventInitiatorDoneTimerExpired(
    struct abn_mac_Responder* responder) {
  abn_sync_Atomic_StoreRelaxed(responder->config->timer_expired, 0);

  responder->config->client_callback(
      responder->config->client_callback_context,
      ABN_MAC_RESPONDER_CALLBACK_OPCODE_TIMER_EXPIRED,
      abn_sync_Atomic_LoadAcquire(responder->config->state),
      0);

  abn_mac_Responder_SetState(responder, ABN_MAC_RESPONDER_STATE_IDLE);
}

static void abn_mac_Responder_ProcessEvent(struct abn_mac_Responder* responder,
                                           uint8_t event) {
  abn_sync_Atomic_StoreRelease(responder->config->processing_event, 1);

  switch (event) {
    case ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_SUCCESS:
      abn_mac_Responder_ProcessEventTransferResult(responder, true);
      break;
    case ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_ERROR:
      abn_mac_Responder_ProcessEventTransferResult(responder, false);
      break;
    case ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_IDLE:
      abn_mac_Responder_ProcessEventInitiatorReadyInIdle(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_WAITING:
      abn_mac_Responder_ProcessEventInitiatorReadyInWaiting(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_INITIATOR_DONE:
      abn_mac_Responder_ProcessEventInitiatorDone(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_TX_DATA_AVAILABLE:
      abn_mac_Responder_ProcessEventTxDataAvailable(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_INVALID_STATE:
      abn_mac_Responder_ProcessEventInvalidState(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_TRANSFER_RESULT_TIMER_EXPIRED:
      abn_mac_Responder_ProcessEventTransferResultTimerExpired(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_TIMER_EXPIRED:
      abn_mac_Responder_ProcessEventInitiatorReadyTimerExpired(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_INITIATOR_DONE_TIMER_EXPIRED:
      abn_mac_Responder_ProcessEventInitiatorDoneTimerExpired(responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_INITIATOR_READY_WHILE_IN_WAITING_FOR_INITIATOR_DONE:
      abn_mac_ResponderProcessEventInitiatorActiveWhileWaitingForInitiatorDone(
          responder);
      break;
    case ABN_MAC_RESPONDER_EVENT_NONE:
      break;
  }

  abn_sync_Atomic_StoreRelease(responder->config->processing_event, 0);
}

// External APIs

bool abn_mac_Responder_ProcessOneEvent(struct abn_mac_Responder* responder) {
  enum abn_mac_Responder_Event event;
  ABN_ASSERT(abn_sync_Atomic_LoadAcquire(responder->config->state) !=
             ABN_MAC_RESPONDER_STATE_UNINITIALIZED);

  event = abn_mac_Responder_GetNextEvent(responder);
  if (event == ABN_MAC_RESPONDER_EVENT_NONE) {
    return false;
  }
  abn_mac_Responder_ProcessEvent(responder, event);
  return true;
}

void abn_mac_Responder_ProcessAllEventsAndWaitForNextEvent(
    struct abn_mac_Responder* responder) {
  ABN_ASSERT(responder != NULL);

  // Process all events.
  while (abn_mac_Responder_ProcessOneEvent(responder)) {
  }

  // Wait for the next event.
  abn_sync_ThreadNotification_Acquire(responder->config->event_notification);
}

void abn_mac_Responder_Init(struct abn_mac_Responder* responder,
                            const struct abn_mac_Responder_Config* config,
                            const struct abn_mac_Client* data_client) {
  ABN_ASSERT(responder != NULL);

  responder->config = config;
  responder->data_client = data_client;
  responder->client_transaction_pending = false;

  abn_mac_Responder_ClearPendingVariables(responder);

  // this should be clear here and not in ClearPendingVariables()
  // to avoid any race condition
  abn_sync_Atomic_StoreRelaxed(responder->config->initiator_signal_transitioned,
                               0);

  responder->config->signal_from_initiator->functions->set_transition_handler(
      responder->config->signal_from_initiator->signal_in_handle,
      abn_Responder_InitiatorSignalTransitionHandle,
      responder);

  responder->config->signal_from_initiator->functions
      ->enable_transition_handler(
          responder->config->signal_from_initiator->signal_in_handle, true);

  // if the Initiator signal is already active, we should act on it
  if (responder->config->signal_from_initiator->functions->get(
          responder->config->signal_from_initiator->signal_in_handle) ==
      ABN_SIGNAL_STATE_ACTIVE) {
    abn_sync_Atomic_StoreRelease(
        responder->config->initiator_signal_transitioned, 1);
  }

  responder->config->transport->functions->set_result_handler(
      responder->config->transport->transport_handle,
      abn_mac_Responder_TransportResultHandle,
      responder);

  responder->config->timer->functions->set_expiry_handler(
      responder->config->timer->timer_handle,
      abn_mac_Responder_TimerExpiryHandler,
      responder);

  abn_sync_Atomic_StoreRelease(responder->config->state,
                               ABN_MAC_RESPONDER_STATE_IDLE);

  responder->idle_callback = NULL;
  responder->idle_callback_context = NULL;

  abn_sync_Atomic_StoreRelease(responder->config->processing_event, 0);

  responder->idle_state_reported = ABN_MAC_PM_STATE_UNKNOWN;
}

// TODO: b/313995134 - Disable all pending things: timer, transaction
// Disable callback handles
void abn_mac_Responder_Deinit(struct abn_mac_Responder* responder) {
  ABN_ASSERT(responder != NULL);

  abn_sync_Atomic_StoreRelease(responder->config->state,
                               ABN_MAC_RESPONDER_STATE_UNINITIALIZED);
}

bool abn_mac_Responder_IsIdle(void* context) {
  struct abn_mac_Responder* responder = (struct abn_mac_Responder*)context;

  ABN_ASSERT(responder != NULL);

  // By design, we need to handle the signal independently from Autobahn Core
  // As a result, all we need to do here is to check the state variable
  // this is safe, since state is Atomic
  return (
      (abn_sync_Atomic_LoadAcquire(responder->config->state) ==
       ABN_MAC_RESPONDER_STATE_IDLE) &&
      (abn_sync_Atomic_LoadAcquire(responder->config->processing_event) == 0));
}

void abn_mac_Responder_SetIdleCallback(void* container_handle,
                                       abn_mac_Pm_IdleCallback callback,
                                       void* context) {
  struct abn_mac_Responder* responder =
      (struct abn_mac_Responder*)container_handle;

  ABN_ASSERT(responder != NULL);
  ABN_ASSERT(responder->idle_callback == NULL);

  responder->idle_callback_context = context;
  responder->idle_callback = callback;
}
