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

#include "autobahn/llc/llc.h"

#include "autobahn/assert/assert.h"
#include "autobahn/llc/channel.h"
#include "autobahn/llc/frame.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/llc/status.h"
#include "autobahn/llc/status_update.h"
#include "autobahn/log/log.h"

bool abn_llc_Llc_IsTxDataAvailable(void* client_handle) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)client_handle;

  ABN_DASSERT(llc != NULL);

  if (!llc->init_sent) {
    return true;
  }

  if (llc->response_required) {
    return true;
  }

  if (abn_sync_Atomic_LoadAcquire(
          llc->config->channel_status_update_available) == 1) {
    return true;
  }

  if (abn_sync_Atomic_LoadAcquire(llc->config->channel_data_available) == 1) {
    return true;
  }

  return false;
}

inline static uint8_t abn_llc_Llc_GetPrevIndexInTxFramesQueue(uint8_t index) {
  if (index == 0) {
    return ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW - 1;
  }

  return index - 1;
}

inline static struct abn_llc_Frame* abn_llc_Llc_GetNextTxFrame(
    struct abn_llc_Llc* llc) {
  if (llc->number_valid_in_tx_frames_queue <
      ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW) {
    ++llc->number_valid_in_tx_frames_queue;
  }

  if (llc->sequence_id_enabled) {
    ++llc->newest_frame_index_in_tx_frames_queue;
    if (llc->newest_frame_index_in_tx_frames_queue ==
        ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW) {
      llc->newest_frame_index_in_tx_frames_queue = 0;
    }

    return llc->config
        ->tx_frames_queue[llc->newest_frame_index_in_tx_frames_queue];
  }

  // ACK/NACK case
  llc->newest_frame_index_in_tx_frames_queue =
      1 - llc->newest_frame_index_in_tx_frames_queue;
  return llc->config
      ->tx_frames_queue[llc->newest_frame_index_in_tx_frames_queue];
}

// Precondition: llc->number_pending_retransmissions > 0
inline static int abn_llc_Llc_GetRetransmissionIndexFrame(
    struct abn_llc_Llc* llc) {
  int index_tx_frames_queue = llc->newest_frame_index_in_tx_frames_queue -
                              (llc->number_pending_retransmissions - 1);
  if (index_tx_frames_queue < 0) {
    index_tx_frames_queue =
        ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW + index_tx_frames_queue;
  }

  return index_tx_frames_queue;
}

// This is called only for ACK/NACK mechanism
inline static struct abn_llc_Frame* abn_llc_Llc_GetNackOldestTxFrame(
    struct abn_llc_Llc* llc) {
  if (llc->number_pending_retransmissions ==
      ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW) {
    return llc->config
        ->tx_frames_queue[1 - llc->newest_frame_index_in_tx_frames_queue];
  }

  return llc->config
      ->tx_frames_queue[llc->newest_frame_index_in_tx_frames_queue];
}

// This is called while llc->config->mutex is locked
void abn_llc_Llc_UpdateRxFullBits(struct abn_llc_Llc* llc) {
  unsigned int channel_index;

  for (channel_index = 0; channel_index < ABN_LLC_NUM_CHANNELS;
       channel_index++) {
    if (!abn_llc_StatusUpdate_GetChannelBit(
            &llc->pending_channel_rx_full_status_update, channel_index) ||
        llc->local_channels[channel_index] == NULL) {
      continue;
    }

    bool rx_status =
        llc->local_channels[channel_index]->functions->get_rx_status(
            llc->local_channels[channel_index]->channel_handle);
    abn_llc_Status_SetChannelBit(&llc->local_channels_status,
                                 channel_index,
                                 ABN_LLC_CHANNEL_RX_FULL_BIT,
                                 !rx_status);
  }

  abn_llc_StatusUpdate_Init(&llc->pending_channel_rx_full_status_update);
}

static void abn_llc_PrepareInitFrame(struct abn_llc_Llc* llc,
                                     struct abn_llc_Frame** tx_buffer_out) {
  struct abn_llc_Llc_InitFrame_Data* init_data;
  uint8_t* payload;

  // we need to clear the TX frames queue
  llc->number_valid_in_tx_frames_queue = 0;
  // after we sent INIT frame, we should not send any previous frame(s)
  // since we start a new session
  llc->number_pending_retransmissions = 0;
  // This is not necessary, but we want  to start from a known state
  llc->newest_frame_index_in_tx_frames_queue = 0;

  *tx_buffer_out = abn_llc_Llc_GetNextTxFrame(llc);
  abn_llc_Frame_GetPayloadBuffer(*tx_buffer_out, &payload);
  init_data = (struct abn_llc_Llc_InitFrame_Data*)payload;
  init_data->version = ABN_LLC_LLC_VERSION;

  abn_llc_Frame_PrepareInit(*tx_buffer_out,
                            llc->previous_rx_frame_valid,
                            llc->init_received,
                            llc->expected_incoming_flow_sequence_id,
                            SEQUENCE_ID_INITIAL_VALUE,
                            /*sequence_id_supported*/ true,
                            llc->config->integrity_checker);
  llc->init_sent = true;
  llc->response_required = true;

  llc->last_sent_frame_type = ABN_LLC_FRAME_TYPE_INIT;

  llc->config->client->functions->notification(
      llc->config->client->application_handle,
      ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_SENT);
}

static void abn_llc_PrepareEmptyFrame(struct abn_llc_Llc* llc,
                                      uint8_t current_outgoing_flow_sequence_id,
                                      struct abn_llc_Frame** tx_buffer_out) {
  *tx_buffer_out = abn_llc_Llc_GetNextTxFrame(llc);
  abn_llc_Frame_PrepareEmpty(*tx_buffer_out,
                             llc->previous_rx_frame_valid,
                             llc->expected_incoming_flow_sequence_id,
                             current_outgoing_flow_sequence_id,
                             llc->config->integrity_checker);
  llc->last_sent_frame_type = ABN_LLC_FRAME_TYPE_EMPTY;
  llc->config->client->functions->notification(
      llc->config->client->application_handle,
      ABN_LLC_APPLICATION_CALLBACK_EVENT_EMPTY_SENT);
}

static void abn_llc_PrepareStatusFrame(
    struct abn_llc_Llc* llc,
    uint8_t current_outgoing_flow_sequence_id,
    struct abn_llc_Frame** tx_buffer_out) {
  uint8_t* buffer;
  enum abn_llc_PendingStatusChange* current_pending;
  uint8_t current_index = llc->current_pending_availability_index;
  uint8_t next_index = 1 - current_index;

  abn_sync_Mutex_Lock(llc->config->mutex);

  // Check if there is an update in rx status for any channels and update
  // the RX FULL bits, if needed.
  if (abn_llc_StatusUpdate_Available(
          &llc->pending_channel_rx_full_status_update)) {
    abn_llc_Llc_UpdateRxFullBits(llc);
  }

  // Apply pending availability changes to the local_channels_status.
  current_pending = llc->pending_availability_statuses[current_index];
  for (int i = 0; i < ABN_LLC_NUM_CHANNELS; ++i) {
    // Set to available/not available if a pending change exists.
    if (current_pending[i] == STATUS_CHANGE_AVAILABLE) {
      abn_llc_Status_SetChannelBit(
          &llc->local_channels_status, i, ABN_LLC_CHANNEL_AVAILABLE_BIT, true);
    } else if (current_pending[i] == STATUS_CHANGE_NOT_AVAILABLE) {
      abn_llc_Status_SetChannelBit(
          &llc->local_channels_status, i, ABN_LLC_CHANNEL_AVAILABLE_BIT, false);
    }
  }

  *tx_buffer_out = abn_llc_Llc_GetNextTxFrame(llc);

  abn_llc_Frame_GetPayloadBuffer(*tx_buffer_out, &buffer);
  memcpy(buffer, &llc->local_channels_status, sizeof(struct abn_llc_Status));
  // On purpose here we are not updating the STATUS packet across nacks and
  // invalid packet responses (i.e. unknown ack/ack responses) even though
  // this may reduce maintenance traffic given that we expect a very low
  // nack/invalid-packet rate.
  abn_llc_Frame_PrepareStatus(*tx_buffer_out,
                              llc->previous_rx_frame_valid,
                              sizeof(struct abn_llc_Status),
                              llc->expected_incoming_flow_sequence_id,
                              current_outgoing_flow_sequence_id,
                              llc->config->integrity_checker);

  memset(current_pending,
         STATUS_CHANGE_NONE,
         sizeof(abn_llc_PendingStatusChangeChannels));
  llc->pending_status_updates[current_index] = 0;

  llc->current_pending_availability_index = next_index;

  if (llc->pending_status_updates[llc->current_pending_availability_index] ==
      0) {
    abn_sync_Atomic_StoreRelease(llc->config->channel_status_update_available,
                                 0);
  }

  llc->response_required = true;
  abn_sync_Mutex_Unlock(llc->config->mutex);
  llc->last_sent_frame_type = ABN_LLC_FRAME_TYPE_STATUS;
  llc->config->client->functions->notification(
      llc->config->client->application_handle,
      ABN_LLC_APPLICATION_CALLBACK_EVENT_STATUS_SENT);
}

static void abn_llc_PrepareDataFrame(struct abn_llc_Llc* llc,
                                     uint8_t current_outgoing_flow_sequence_id,
                                     struct abn_llc_Frame** tx_buffer_out) {
  uint8_t index_channel;
  uint8_t* buffer;
  size_t buffer_size;
  bool additional_data_available;

  abn_sync_Mutex_Lock(llc->config->mutex);
  index_channel = abn_llc_ChannelPriorityList_GetNextChannelIndex(
      &llc->channels_with_tx_available_in_decreasing_priority,
      &llc->current_channel_in_priority_list_index);

  *tx_buffer_out = abn_llc_Llc_GetNextTxFrame(llc);
  abn_llc_Frame_GetPayloadBuffer(*tx_buffer_out, &buffer);
  // The channel from the priority list must have data to send.
  ABN_ASSERT(llc->local_channels[index_channel]->functions->get_tx_datagram(
      llc->local_channels[index_channel]->channel_handle,
      buffer,
      &buffer_size,
      &additional_data_available));
  abn_llc_Frame_PrepareData(*tx_buffer_out,
                            llc->previous_rx_frame_valid,
                            index_channel,
                            buffer_size,
                            llc->expected_incoming_flow_sequence_id,
                            current_outgoing_flow_sequence_id,
                            llc->config->integrity_checker);
  llc->response_required = true;
  if (!additional_data_available) {
    abn_llc_ChannelPriorityList_RemoveFromList(
        &llc->channels_with_tx_available_in_decreasing_priority, index_channel);
    abn_llc_ChannelMask_Set(
        &llc->local_channels_with_tx_available_and_remote_rx_available,
        index_channel,
        false);
    if (llc->channels_with_tx_available_in_decreasing_priority
            .number_channels_in_priority_list == 0) {
      abn_sync_Atomic_StoreRelease(llc->config->channel_data_available, 0);
    }
  }
  llc->last_sent_frame_type = ABN_LLC_FRAME_TYPE_DATAGRAM;
  abn_sync_Mutex_Unlock(llc->config->mutex);
  llc->config->client->functions->notification(
      llc->config->client->application_handle,
      ABN_LLC_APPLICATION_CALLBACK_EVENT_DATA_SENT);
}

// Precondition: llc->sequence_id_enabled == true
static struct abn_llc_Frame* abn_llc_Llc_GetLastTransmittedFrame(
    struct abn_llc_Llc* llc) {
  int index_tx_frames_queue;

  if (llc->sequence_id_enabled) {
    if (llc->number_pending_retransmissions == 0) {
      return llc->config
          ->tx_frames_queue[llc->newest_frame_index_in_tx_frames_queue];
    } else {
      index_tx_frames_queue = abn_llc_Llc_GetRetransmissionIndexFrame(llc);
      return llc->config->tx_frames_queue[index_tx_frames_queue];
    }
  } else {
    return llc->config
        ->tx_frames_queue[llc->newest_frame_index_in_tx_frames_queue];
  }
}

void abn_llc_Llc_PrepareBuffers(void* client_handle,
                                struct abn_mac_Frame** tx_buffer_out,
                                struct abn_mac_Frame** rx_buffer_out) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)client_handle;
  uint8_t current_outgoing_flow_sequence_id;

  ABN_DASSERT(llc != NULL);
  ABN_DASSERT(tx_buffer_out != NULL);
  ABN_DASSERT(rx_buffer_out != NULL);

  *rx_buffer_out = (struct abn_mac_Frame*)llc->config->rx_frame;

  if (llc->last_transaction_failed) {
    // If the previous frame received was invalid, we resend the same frame
    // since we don't know if the other side received the previous one or not.
    // If the other side already received it, the second one is going
    // to be ignored because of the sequence ID
    *tx_buffer_out =
        (struct abn_mac_Frame*)abn_llc_Llc_GetLastTransmittedFrame(llc);
    llc->last_transaction_failed = false;
    return;
  }

  llc->prepared_buffers = true;

  if (!llc->init_sent) {
    abn_llc_PrepareInitFrame(llc, (struct abn_llc_Frame**)tx_buffer_out);
    return;
  }

  if (!llc->sequence_id_enabled) {
    // If the previous frame was invalid, we need to send an EMPTY frame
    if (!llc->previous_rx_frame_valid) {
      abn_llc_PrepareEmptyFrame(llc, 0, (struct abn_llc_Frame**)tx_buffer_out);
      return;
    }
  }

  // number_pending_retransmissions is decremented after the transaction is
  // done, in abn_llc_Llc_ProcessExpectedOutgoingFlowSequenceId()

  if (llc->sequence_id_enabled) {
    if (llc->number_pending_retransmissions > 0) {
      *tx_buffer_out =
          (struct abn_mac_Frame*)llc->config
              ->tx_frames_queue[abn_llc_Llc_GetRetransmissionIndexFrame(llc)];

      abn_llc_Frame_SetAckRxSequenceIdAndUpdateIntegrityCheck(
          (struct abn_llc_Frame*)*tx_buffer_out,
          llc->expected_incoming_flow_sequence_id,
          llc->previous_rx_frame_valid,
          llc->config->integrity_checker);
      llc->last_sent_frame_type =
          abn_llc_Frame_GetType((struct abn_llc_Frame*)*tx_buffer_out);
      return;
    }  // llc->number_pending_retransmissions > 0)
  } else {  // !llc->sequence_id_enabled
    if (llc->number_pending_retransmissions > 0) {
      // If there are pending TX frames because of a previous ack=false,
      // we consume from those frames and don't prepare a new one
      // After the frame is sent, newest_frame_index_in_tx_frames_queue points
      // to the last frame sent
      // When we have two pending frames to retransmit, we need to switch before
      // accesing which TX frame to send (that's why 'true' for
      // abn_llc_Llc_GetNextTxFrame()) because we want to retransmit them in
      // order, starting with the oldest one. When we have only one, we don't
      // need to switch, we send the last one which was sent

      // This is a corner case:
      // We sent INIT only (so number_pending_retransmissions)
      // we received back ack=false
      // we need to send the exact tx_frames_queue sent before, not the one
      // which was never sent. That's why we don't switch it when there is only
      // one pending frame
      *tx_buffer_out =
          (struct abn_mac_Frame*)abn_llc_Llc_GetNackOldestTxFrame(llc);

      // we need to set the latest previous_rx_frame_valid
      // eventually, we can add Frame_SetAck() and call directly that one
      abn_llc_Frame_SetAckAndUpdateIntegrityCheck(
          (struct abn_llc_Frame*)*tx_buffer_out,
          llc->previous_rx_frame_valid,
          llc->config->integrity_checker);
      --llc->number_pending_retransmissions;
      llc->last_sent_frame_type =
          abn_llc_Frame_GetType((struct abn_llc_Frame*)*tx_buffer_out);
      return;
    }  // llc->number_pending_retransmissions > 0)
  }

  // At this point we already sent the INIT frame.
  current_outgoing_flow_sequence_id =
      abn_llc_Frame_GetNextSequenceId(abn_llc_Frame_GetOutgoingSequenceId(
          abn_llc_Llc_GetLastTransmittedFrame(llc)));

  // If we haven't received the INIT frame, we send the INIT frame
  if (!llc->init_received) {
    // TODO b/412943683: Revert this logic after gpar/1246520 is deployed to
    // AP and MCU.

    // Send empty frame for a while, to allow the other side to process
    // all pending callbacks before reinit.
    if (llc->empty_frame_count < llc->config->reinit_window_empty_frame_count) {
      ++llc->empty_frame_count;
      abn_llc_PrepareEmptyFrame(llc,
                                current_outgoing_flow_sequence_id,
                                (struct abn_llc_Frame**)tx_buffer_out);
      return;
    }

    // We're accepting their incoming data while we wait on their INIT packet
    // at which point we reset the sequence IDs to ensure that they don't end up
    // retransmissing data to us while we wait on their INIT
    llc->empty_frame_count = 0;
    abn_llc_PrepareInitFrame(llc, (struct abn_llc_Frame**)tx_buffer_out);
    return;
  }

  // We need to lock here, since channels_status_update might get updated on
  // channel clients threads
  if (abn_sync_Atomic_LoadAcquire(
          llc->config->channel_status_update_available) == 1) {
    abn_llc_PrepareStatusFrame(llc,
                               current_outgoing_flow_sequence_id,
                               (struct abn_llc_Frame**)tx_buffer_out);
    return;
  }

  if (llc->next_transfer_suppress_data_frame) {
    abn_llc_PrepareEmptyFrame(llc,
                              current_outgoing_flow_sequence_id,
                              (struct abn_llc_Frame**)tx_buffer_out);
    llc->next_transfer_suppress_data_frame = false;
    return;
  }

  if (abn_sync_Atomic_LoadAcquire(llc->config->channel_data_available) == 1) {
    abn_llc_PrepareDataFrame(llc,
                             current_outgoing_flow_sequence_id,
                             (struct abn_llc_Frame**)tx_buffer_out);
    return;
  }

  llc->response_required = false;
  // No need to increment number_valid_in_tx_frames_queue here, since we
  // send an EMPTY frame

  abn_llc_PrepareEmptyFrame(llc,
                            current_outgoing_flow_sequence_id,
                            (struct abn_llc_Frame**)tx_buffer_out);
}

// This is called while llc->config->mutex is locked
static void abn_llc_Llc_ReInitChannels(struct abn_llc_Llc* llc) {
  int i;
  struct abn_llc_Channel* temp_channel;
  uint8_t current_index = llc->current_pending_availability_index;
  uint8_t next_index = 1 - current_index;

  enum abn_llc_PendingStatusChange* current_pending =
      llc->pending_availability_statuses[current_index];
  enum abn_llc_PendingStatusChange* next_pending =
      llc->pending_availability_statuses[next_index];

  abn_llc_ChannelPriorityList_Init(
      &llc->channels_with_tx_available_in_decreasing_priority);
  llc->current_channel_in_priority_list_index = 0;
  abn_llc_ChannelMask_Init(
      &llc->local_channels_with_tx_available_and_remote_rx_unavailable);
  abn_llc_ChannelMask_Init(
      &llc->local_channels_with_tx_available_and_remote_rx_available);
  abn_sync_Atomic_StoreRelease(llc->config->channel_data_available, 0);
  abn_llc_ChannelMask_Init(&llc->local_channel_close_pending_remote_close_ack);

  for (i = 0; i < ABN_LLC_NUM_CHANNELS; ++i) {
    if ((llc->local_channels[i] != NULL) &&
        abn_llc_Status_GetChannelBit(
            &llc->remote_channels_status, i, ABN_LLC_CHANNEL_AVAILABLE_BIT)) {
      abn_llc_Status_SetChannelBit(
          &llc->local_channels_status, i, ABN_LLC_CHANNEL_AVAILABLE_BIT, false);
      temp_channel = llc->local_channels[i];
      llc->local_channels[i] = NULL;
      --llc->number_locally_available_channels;

      current_pending[i] = STATUS_CHANGE_NONE;
      next_pending[i] = STATUS_CHANGE_NONE;

      temp_channel->functions->handle_connection_callback(
          temp_channel->channel_handle, false);
    }
  }

  memset(llc->pending_status_updates, 0, sizeof(llc->pending_status_updates));

  abn_llc_Status_Init(&llc->remote_channels_status);
}

void abn_llc_Llc_ProcessInitFrame(struct abn_llc_Llc* llc,
                                  struct abn_llc_Frame* frame) {
  // If we already received INIT before, this means
  // the other side rebooted and it is going to expect
  // to receive INIT, too
  struct abn_llc_Llc_InitFrame_Data* init_data;
  size_t init_size;

  // if we receive an INIT frame without INIT_RECEIVED set
  // we assume the other side rebooted, so we need to send INIT again
  // unless we send INIT on the same transaction
  if (!abn_llc_Frame_GetInitReceived(frame)) {
    if (llc->last_sent_frame_type != ABN_LLC_FRAME_TYPE_INIT) {
      llc->init_sent = false;
    }
  }

  llc->init_received = true;
  llc->empty_frame_count = 0;

  // We synchronize expected_incoming_flow_sequence_id with the other side
  llc->expected_incoming_flow_sequence_id = abn_llc_Frame_GetNextSequenceId(
      abn_llc_Frame_GetOutgoingSequenceId(frame));

  abn_sync_Mutex_Lock(llc->config->mutex);

  llc->config->client->functions->notification(
      llc->config->client->application_handle,
      ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_RECEIVED);

  abn_llc_Frame_GetPayload(frame, (uint8_t**)&init_data, &init_size);

  if (init_data->version != ABN_LLC_LLC_VERSION) {
    llc->config->client->functions->notification(
        llc->config->client->application_handle,
        ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_VERSION);
    ABN_ASSERT(false);
  }

  // By receiving INIT frame, we assume the other side rebooted
  // so we should clean all pending variables
  llc->response_required = true;
  llc->ignore_next_rx_frame = false;

  llc->sequence_id_enabled = abn_llc_Frame_IsSequenceIdSupported(frame);

  // If we receved INIT frame because of the other side rebooted,
  // we need to re-send the STATUS frame, so the other side
  // is aware of the channels which are open already
  abn_sync_Atomic_StoreRelease(llc->config->channel_status_update_available, 1);
  // for the channels was already opened, we need to inform about a new INIT
  // frame which should force the clients to close/open all channels
  // We get out of the loop once we reach the number of open channels
  abn_llc_Llc_ReInitChannels(llc);
  abn_sync_Mutex_Unlock(llc->config->mutex);
}

// This function is called while llc->config->mutex is already locked.
static void abn_llc_Llc_RequestRemoveChannelLocked(struct abn_llc_Llc* llc,
                                                   uint8_t channel_number) {
  // since the function could be called multiple times
  // the object might be NULL already
  if (llc->local_channels[channel_number] == NULL) {
    return;
  }

  // we need to remove the channel here from the list of tx data channels
  // so we will not try to get data from it the next PrepareBuffer() call
  if (abn_llc_ChannelPriorityList_RemoveFromList(
          &llc->channels_with_tx_available_in_decreasing_priority,
          channel_number)) {
    if (llc->channels_with_tx_available_in_decreasing_priority
            .number_channels_in_priority_list == 0) {
      abn_sync_Atomic_StoreRelease(llc->config->channel_data_available, 0);
    }
  }

  abn_llc_ChannelMask_Set(
      &llc->remove_channel_pending_flags, channel_number, true);
  abn_llc_ChannelMask_Set(
      &llc->local_channel_close_pending_remote_close_ack, channel_number, true);

  abn_llc_Llc_CallbackNotification(
      llc, channel_number, llc->local_channels[channel_number]);

  llc->local_channels[channel_number]->functions->handle_connection_callback(
      llc->local_channels[channel_number]->channel_handle, false);
}

static void abn_llc_ProcessRemoteChannelsStatus(struct abn_llc_Llc* llc,
                                                struct abn_llc_Status* status) {
  int i, j;
  // the new status of a remote channel bit
  // (included in the framejust received)
  bool new_remote_status;
  // the existing status of a remote channel bit
  // (known before last frame received)
  bool current_remote_status;
  struct abn_llc_Channel* channel;

  // We go through all the channels which are opened
  // Once we reach the number of open channels, we exit the loop
  abn_sync_Mutex_Lock(llc->config->mutex);
  for (i = 0, j = 0; (i < ABN_LLC_NUM_CHANNELS) &&
                     (j < llc->number_locally_available_channels);
       ++i) {
    if (llc->local_channels[i] != NULL) {
      // true when the remote side can't receive data
      new_remote_status =
          abn_llc_Status_GetChannelBit(status, i, ABN_LLC_CHANNEL_RX_FULL_BIT);
      // true when previously we know that remote side can't receive data
      current_remote_status = abn_llc_Status_GetChannelBit(
          &llc->remote_channels_status, i, ABN_LLC_CHANNEL_RX_FULL_BIT);
      // when there is a difference between the known status and the new
      // status we need to process the difference
      if (new_remote_status != current_remote_status) {
        if (new_remote_status) {
          if (abn_llc_ChannelMask_Get(
                  &llc->local_channels_with_tx_available_and_remote_rx_available,
                  i)) {
            abn_llc_ChannelPriorityList_RemoveFromList(
                &llc->channels_with_tx_available_in_decreasing_priority, i);
            abn_llc_ChannelMask_Set(
                &llc->local_channels_with_tx_available_and_remote_rx_available,
                i,
                false);
            abn_llc_ChannelMask_Set(
                &llc->local_channels_with_tx_available_and_remote_rx_unavailable,
                i,
                true);
          }
        } else {
          if (abn_llc_ChannelMask_Get(
                  &llc->local_channels_with_tx_available_and_remote_rx_unavailable,
                  i)) {
            abn_llc_ChannelPriorityList_AddToList(
                &llc->channels_with_tx_available_in_decreasing_priority, i);
            abn_sync_Atomic_StoreRelease(llc->config->channel_data_available,
                                         1);
            abn_llc_ChannelMask_Set(
                &llc->local_channels_with_tx_available_and_remote_rx_available,
                i,
                true);
            abn_llc_ChannelMask_Set(
                &llc->local_channels_with_tx_available_and_remote_rx_unavailable,
                i,
                false);
          }
        }
      }

      // true when the state of the remote channel reported is that the
      // channel is available
      new_remote_status = abn_llc_Status_GetChannelBit(
          status, i, ABN_LLC_CHANNEL_AVAILABLE_BIT);
      // true when the last known state of the remote channel is that the
      // channel is available
      current_remote_status = abn_llc_Status_GetChannelBit(
          &llc->remote_channels_status, i, ABN_LLC_CHANNEL_AVAILABLE_BIT);
      if (new_remote_status != current_remote_status) {
        // if the channel is closed remotely and it was already opened
        // locally, we close the channel locally and we notify the client. the
        // client should be opened it again, if necessary
        channel = llc->local_channels[i];
        if (!new_remote_status) {
          if (abn_llc_ChannelMask_Get(
                  &llc->local_channel_close_pending_remote_close_ack, i)) {
            // This is an acknowledgement of a locally-initiated close.
            // We clear the flag and do not propagate another close event.
            abn_llc_ChannelMask_Set(
                &llc->local_channel_close_pending_remote_close_ack, i, false);
          } else {
            // This is a remotely-initiated close.
            abn_llc_Llc_RequestRemoveChannelLocked(llc, i);
          }
        } else {
          channel->functions->handle_connection_callback(
              channel->channel_handle, true);
        }
      }
      ++j;
    }
  }
  memcpy(&llc->remote_channels_status, status, sizeof(struct abn_llc_Status));

  abn_sync_Atomic_StoreRelease(
      llc->config->channel_data_available,
      (llc->channels_with_tx_available_in_decreasing_priority
           .number_channels_in_priority_list != 0));

  abn_sync_Mutex_Unlock(llc->config->mutex);

  llc->config->client->functions->notification(
      llc->config->client->application_handle,
      ABN_LLC_APPLICATION_CALLBACK_EVENT_STATUS_RECEIVED);
}

void abn_llc_Llc_ProcessDataFrame(struct abn_llc_Llc* llc,
                                  struct abn_llc_Frame* frame) {
  uint8_t channel_index;
  uint8_t* buffer;
  size_t buffer_size;
  enum abn_llc_ProcessRxDatagram_ReturnValue ret;

  channel_index = frame->header.channel;
  abn_llc_Frame_GetPayload(frame, &buffer, &buffer_size);

  llc->config->client->functions->notification(
      llc->config->client->application_handle,
      ABN_LLC_APPLICATION_CALLBACK_EVENT_DATA_RECEIVED);

  abn_sync_Mutex_Lock(llc->config->mutex);
  if (llc->local_channels[channel_index] == NULL) {
    // We may receive data for a channel which was just closed
    // before we could exchange a status frame with the other side.
    abn_sync_Mutex_Unlock(llc->config->mutex);
    return;
  }
  ret = llc->local_channels[channel_index]->functions->process_rx_datagram(
      llc->local_channels[channel_index]->channel_handle, buffer, buffer_size);

  switch (ret) {
    case ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_RX_FULL:
      abn_llc_StatusUpdate_SetChannelBit(
          &llc->pending_channel_rx_full_status_update, channel_index, true);
      abn_sync_Atomic_StoreRelease(llc->config->channel_status_update_available,
                                   1);
      abn_sync_Mutex_Unlock(llc->config->mutex);
      break;
    case ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_FAIL:
      // This is an error case where we fail to process the RX data and
      // as a result there is a data loss. The only way to recover is to close
      // the channel, so the client gets notified and eventually re-open the
      // channel.
      abn_llc_Llc_RequestRemoveChannelLocked(llc, channel_index);
      abn_sync_Mutex_Unlock(llc->config->mutex);
      break;
    case ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_SUCCESS:
      abn_sync_Mutex_Unlock(llc->config->mutex);
      break;
  }
}

static void abn_llc_Llc_ProcessCurrentIncomingFlowSequenceId(
    struct abn_llc_Llc* llc,
    const struct abn_llc_Frame* received_frame,
    bool* ignore_frame,
    bool* reinit_required) {
  uint8_t current_incoming_flow_sequence_id;

  *ignore_frame = false;
  llc->next_transfer_suppress_data_frame = false;

  current_incoming_flow_sequence_id =
      abn_llc_Frame_GetOutgoingSequenceId(received_frame);

  if (current_incoming_flow_sequence_id ==
      llc->expected_incoming_flow_sequence_id) {
    llc->expected_incoming_flow_sequence_id = abn_llc_Frame_GetNextSequenceId(
        llc->expected_incoming_flow_sequence_id);

    return;
  }

  // If the sequence ID received is different from the expected one by 1
  // (plus or minus) we just ignore it, assuming that there was a frame
  // duplicated or a frame lost
  if ((current_incoming_flow_sequence_id ==
       abn_llc_Frame_GetNextSequenceId(
           llc->expected_incoming_flow_sequence_id)) ||
      (abn_llc_Frame_GetNextSequenceId(current_incoming_flow_sequence_id) ==
       llc->expected_incoming_flow_sequence_id)) {
    *ignore_frame = true;
    // If the incoming frame was out of sequence, send an empty frame on
    // the next transfer. Otherwise, we may be missing a status frame and
    // we might break backpressure control.
    llc->next_transfer_suppress_data_frame = true;
    // We force a follow up transaction, and since we ask again for
    // expected_incoming_flow_sequence_id the other side will send the
    // correct frame
    llc->response_required = true;

    ABN_LOG_ERROR("Incoming flow sequence ID mismatch (expected %d, remote %d)",
                  llc->expected_incoming_flow_sequence_id,
                  current_incoming_flow_sequence_id);
    llc->config->client->functions->notification(
        llc->config->client->application_handle,
        ABN_LLC_APPLICATION_CALLBACK_EVENT_EXPECTED_VS_INCOMING_SEQUENCE_ID_MISMATCH);

    return;
  }

  *reinit_required = true;

  ABN_LOG_ERROR(
      "Fatal: incoming flow sequence ID mismatch (expected %d, remote %d). "
      "Reinitializing LLC.",
      llc->expected_incoming_flow_sequence_id,
      current_incoming_flow_sequence_id);
}

static void abn_llc_Llc_ProcessExpectedOutgoingFlowSequenceId(
    struct abn_llc_Llc* llc,
    const struct abn_llc_Frame* received_frame,
    bool* reinit_required) {
  int number_of_frames_ago, index_in_tx_frames_queue;
  uint8_t outgoing_sequence_id, remote_expected_outgoing_flow_sequence_id,
      current_outgoing_flow_sequence_id;

  remote_expected_outgoing_flow_sequence_id =
      abn_llc_Frame_GetExpectedSequenceId(received_frame);
  current_outgoing_flow_sequence_id = abn_llc_Frame_GetOutgoingSequenceId(
      (struct abn_llc_Frame*)abn_llc_Llc_GetLastTransmittedFrame(llc));

  // If the remote expected sequence is ahead by 1, then we did a redundant
  // retransmission, so it's safe to carry on.
  if ((remote_expected_outgoing_flow_sequence_id ==
       current_outgoing_flow_sequence_id) ||
      (remote_expected_outgoing_flow_sequence_id ==
       abn_llc_Frame_GetNextSequenceId(current_outgoing_flow_sequence_id))) {
    if (llc->number_pending_retransmissions > 0) {
      --llc->number_pending_retransmissions;
    }
    return;
  }

  index_in_tx_frames_queue = llc->newest_frame_index_in_tx_frames_queue;

  for (number_of_frames_ago = 1;
       number_of_frames_ago < llc->number_valid_in_tx_frames_queue;
       number_of_frames_ago++) {
    index_in_tx_frames_queue =
        abn_llc_Llc_GetPrevIndexInTxFramesQueue(index_in_tx_frames_queue);
    outgoing_sequence_id = abn_llc_Frame_GetOutgoingSequenceId(
        llc->config->tx_frames_queue[index_in_tx_frames_queue]);
    if (outgoing_sequence_id == remote_expected_outgoing_flow_sequence_id) {
      llc->number_pending_retransmissions = number_of_frames_ago + 1;

      ABN_LOG_WARN(
          "Outgoing sequence ID mismatch (expected %d, remote %d) "
          "Retransmitting.",
          current_outgoing_flow_sequence_id,
          remote_expected_outgoing_flow_sequence_id);
      llc->config->client->functions->notification(
          llc->config->client->application_handle,
          ABN_LLC_APPLICATION_CALLBACK_EVENT_OUTGOING_VS_EXPECTED_SEQUENCE_ID_MISMATCH);
      return;
    }
  }

  *reinit_required = true;

  ABN_LOG_ERROR(
      "Fatal: outgoing sequence ID mismatch (expected %d "
      "remote %d). Reinitializing LLC.",
      current_outgoing_flow_sequence_id,
      remote_expected_outgoing_flow_sequence_id);
}

void abn_llc_Llc_ProcessRxData(void* client_handle,
                               struct abn_mac_Frame* rx_frame) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)client_handle;
  struct abn_llc_Frame* frame = (struct abn_llc_Frame*)rx_frame;
  uint8_t* buffer;
  size_t buffer_size;
  bool reinit_required = false;
  bool ignore_frame = false;
  enum abn_llc_FrameType frame_type;

  ABN_DASSERT(llc != NULL);
  ABN_DASSERT(rx_frame != NULL);
  ABN_ASSERT(!llc->last_transaction_failed);
  ABN_ASSERT(llc->prepared_buffers);

  llc->prepared_buffers = false;

  // After a bad ack reported, we need to ignore the next frame
  // since the previous one (the invalid one) is going to be sent again
  // and after that the current one again
  // We set previous_rx_frame_valid=true, since the other side is going to
  // resend the previous frame followed by current frame anyway
  if (!llc->sequence_id_enabled) {
    if (llc->ignore_next_rx_frame) {
      llc->ignore_next_rx_frame = false;
      llc->previous_rx_frame_valid = true;
      return;
    }
  }

  if (!abn_llc_Frame_IsValid(frame, llc->config->integrity_checker)) {
    if (!llc->sequence_id_enabled) {
      // If the frame is not valid, we want to ignore the next received frame,
      // however if we have not received an init frame yet then we do not want
      // to ignore the next RX frame as this may be the INIT frame which we're
      // still waiting on.
      llc->ignore_next_rx_frame = llc->init_received;
      llc->number_pending_retransmissions =
          llc->number_valid_in_tx_frames_queue;
    } else {
      if (llc->number_pending_retransmissions == 0) {
        llc->number_pending_retransmissions = 1;
      }  // else we rely on the existing retransmission window.
    }

    llc->previous_rx_frame_valid = false;
    llc->response_required = true;
    llc->config->client->functions->notification(
        llc->config->client->application_handle,
        ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_FRAME_RECEIVED);
    return;
  }

  frame_type = abn_llc_Frame_GetType(frame);

  llc->previous_rx_frame_valid = true;
  if (!llc->sequence_id_enabled) {
    llc->ignore_next_rx_frame = false;

    // If ack = false, it means the previous frame was invalid
    // so we need to re-send last two frames (or less, based on
    // max_number_pending_retransmissions)
    if (!abn_llc_Frame_GetAck(frame)) {
      llc->config->client->functions->notification(
          llc->config->client->application_handle,
          ABN_LLC_APPLICATION_CALLBACK_EVENT_NACK_RECEIVED);
      llc->number_pending_retransmissions =
          llc->number_valid_in_tx_frames_queue;
    }
  }

  // If we haven't received INIT frame yet, we ignore the frame
  // since the channels will close/open on INIT and later re-open/close on
  // STATUS.
  if (!llc->init_received && (frame_type != ABN_LLC_FRAME_TYPE_INIT)) {
    return;
  }

  if (frame_type == ABN_LLC_FRAME_TYPE_INIT) {
    abn_llc_Llc_ProcessInitFrame(llc, frame);
    return;
  }

  if (llc->sequence_id_enabled) {
    abn_llc_Llc_ProcessCurrentIncomingFlowSequenceId(
        llc, frame, &ignore_frame, &reinit_required);

    abn_llc_Llc_ProcessExpectedOutgoingFlowSequenceId(
        llc, frame, &reinit_required);

    if (reinit_required) {
      ABN_LOG_WARN("Forcing reinitialization due to sequence ID failures");
      abn_llc_Llc_ReInit(llc);
      return;
    }

    if (ignore_frame) {
      return;
    }
  }

  if (frame_type == ABN_LLC_FRAME_TYPE_EMPTY) {
    llc->config->client->functions->notification(
        llc->config->client->application_handle,
        ABN_LLC_APPLICATION_CALLBACK_EVENT_EMPTY_RECEIVED);
  } else if (frame_type == ABN_LLC_FRAME_TYPE_DATAGRAM) {
    abn_llc_Llc_ProcessDataFrame(llc, frame);
  } else if (frame_type == ABN_LLC_FRAME_TYPE_STATUS) {
    abn_llc_Frame_GetPayload(frame, &buffer, &buffer_size);
    ABN_ASSERT(buffer_size == sizeof(struct abn_llc_Status));
    abn_llc_ProcessRemoteChannelsStatus(llc, (struct abn_llc_Status*)buffer);
  } else {
    // All frame types should be handled already.
    ABN_LOG_ERROR("Fatal: Unknown frame type: %d! Reinitializing LLC.",
                  frame_type);
    abn_llc_Llc_ReInit(llc);
  }
}

void abn_llc_Llc_HandleTransactionFailure(void* client_handle) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)client_handle;

  ABN_DASSERT(llc != NULL);

  if (llc->prepared_buffers) {
    llc->last_transaction_failed = true;
  }
}

void abn_llc_Llc_Init(struct abn_llc_Llc* llc,
                      const struct abn_llc_LLc_Config* config,
                      const struct abn_mac_Mac* mac,
                      const struct abn_llc_Llc_Proxy_Client* proxy,
                      const struct abn_mac_Pm* mac_pm) {
  ABN_ASSERT(llc != NULL);

  llc->mac = mac;
  llc->config = config;
  llc->proxy = proxy;
  llc->mac_pm = mac_pm;

  llc->init_sent = false;
  llc->init_received = false;
  llc->previous_rx_frame_valid = true;
  llc->response_required = false;
  llc->number_pending_retransmissions = 0;
  llc->last_transaction_failed = false;
  llc->number_valid_in_tx_frames_queue = 0;
  llc->ignore_next_rx_frame = false;
  llc->newest_frame_index_in_tx_frames_queue = -1;
  abn_sync_Atomic_StoreRelease(llc->config->channel_status_update_available, 0);
  abn_sync_Atomic_StoreRelease(llc->config->channel_data_available, 0);
  abn_llc_Status_Init(&llc->local_channels_status);
  abn_llc_Status_Init(&llc->remote_channels_status);
  abn_llc_StatusUpdate_Init(&llc->pending_channel_rx_full_status_update);
  llc->number_locally_available_channels = 0;
  memset(llc->local_channels, 0, sizeof(llc->local_channels));
  abn_llc_ChannelPriorityList_Init(
      &llc->channels_with_tx_available_in_decreasing_priority);
  llc->current_channel_in_priority_list_index = 0;
  llc->prepared_buffers = false;

  llc->last_sent_frame_type = ABN_LLC_FRAME_TYPE_EMPTY;

  llc->pending_callbacks_head_node = NULL;
  llc->pending_callbacks_tail_node = NULL;

  llc->available_nodes_pool_head_node = llc->config->head_pool_nodes;

  abn_llc_ChannelMask_Init(&llc->callbacks_pending_nodes_flags);

  abn_llc_ChannelMask_Init(&llc->remove_channel_pending_flags);

  abn_llc_ChannelMask_Init(
      &llc->local_channels_with_tx_available_and_remote_rx_unavailable);
  abn_llc_ChannelMask_Init(
      &llc->local_channels_with_tx_available_and_remote_rx_available);

  // Since we need to send INIT frame, we should notify Mac about that
  llc->mac->functions->signal_tx_data_available(llc->mac->mac_handle);

  llc->idle_state_reported = ABN_LLC_PM_STATE_UNKNOWN;
  llc->empty_frame_count = 0;

  llc->mac_pm->functions->set_idle_callback(
      llc->mac_pm->mac_handle, abn_llc_Llc_Pm_MacIdleCallback, llc);

  llc->expected_incoming_flow_sequence_id = SEQUENCE_ID_INITIAL_VALUE;

  llc->sequence_id_enabled = false;

  memset(llc->pending_availability_statuses,
         STATUS_CHANGE_NONE,
         sizeof(llc->pending_availability_statuses));
  llc->current_pending_availability_index = 0;
  memset(llc->pending_status_updates, 0, sizeof(llc->pending_status_updates));

  abn_llc_ChannelMask_Init(&llc->local_channel_close_pending_remote_close_ack);
}

void abn_llc_Llc_ReInit(struct abn_llc_Llc* llc) {
  ABN_ASSERT(llc != NULL);

  llc->init_sent = false;
  llc->init_received = false;
  llc->previous_rx_frame_valid = true;
  llc->response_required = false;
  llc->number_pending_retransmissions = 0;
  llc->last_transaction_failed = false;
  llc->number_valid_in_tx_frames_queue = 0;
  llc->ignore_next_rx_frame = false;
  llc->empty_frame_count = 0;
  llc->newest_frame_index_in_tx_frames_queue = -1;

  llc->expected_incoming_flow_sequence_id = SEQUENCE_ID_INITIAL_VALUE;

  llc->sequence_id_enabled = false;

  abn_llc_Llc_ReInitChannels(llc);
}

void abn_llc_Llc_ReInitLocked(struct abn_llc_Llc* llc) {
  ABN_ASSERT(llc != NULL);

  abn_sync_Mutex_Lock(llc->config->mutex);
  abn_llc_Llc_ReInit(llc);
  abn_sync_Mutex_Unlock(llc->config->mutex);
}

static void abn_llc_Llc_UpdatePendingAvailability(struct abn_llc_Llc* llc,
                                                  uint8_t channel_number,
                                                  bool is_available) {
  enum abn_llc_PendingStatusChange new_status =
      is_available ? STATUS_CHANGE_AVAILABLE : STATUS_CHANGE_NOT_AVAILABLE;
  uint8_t current_index = llc->current_pending_availability_index;
  uint8_t next_index = 1 - current_index;

  enum abn_llc_PendingStatusChange* current_pending =
      llc->pending_availability_statuses[current_index];
  enum abn_llc_PendingStatusChange* next_pending =
      llc->pending_availability_statuses[next_index];

  if (current_pending[channel_number] == STATUS_CHANGE_NONE &&
      next_pending[channel_number] == STATUS_CHANGE_NONE) {
    current_pending[channel_number] = new_status;
    ++llc->pending_status_updates[current_index];
  } else if (current_pending[channel_number] == STATUS_CHANGE_AVAILABLE &&
             next_pending[channel_number] == STATUS_CHANGE_NONE &&
             new_status == STATUS_CHANGE_NOT_AVAILABLE) {
    current_pending[channel_number] = STATUS_CHANGE_NONE;
    --llc->pending_status_updates[current_index];
  } else if (current_pending[channel_number] == STATUS_CHANGE_NOT_AVAILABLE &&
             next_pending[channel_number] == STATUS_CHANGE_NONE &&
             new_status == STATUS_CHANGE_AVAILABLE) {
    next_pending[channel_number] = STATUS_CHANGE_AVAILABLE;
    ++llc->pending_status_updates[next_index];
  } else if (current_pending[channel_number] == STATUS_CHANGE_NOT_AVAILABLE &&
             next_pending[channel_number] == STATUS_CHANGE_AVAILABLE &&
             new_status == STATUS_CHANGE_NOT_AVAILABLE) {
    next_pending[channel_number] = STATUS_CHANGE_NONE;
    --llc->pending_status_updates[next_index];
  } else {
    // All valid state transitions should be handled
#if ABN_DEBUG_ASSERT_ENABLED
    ABN_LOG_FATAL(
        "PendingUpdate error: State not allowed "
        "( current %d next %d new %d)",
        current_pending[channel_number],
        next_pending[channel_number],
        new_status);
#else
    ABN_LOG_CRITICAL(
        "PendingUpdate error: State not allowed "
        "(current %d next %d new %d)",
        current_pending[channel_number],
        next_pending[channel_number],
        new_status);
    abn_llc_Llc_ReInitLocked(llc);
    return;
#endif  // ABN_DEBUG_ASSERT_ENABLED
  }

  abn_sync_Atomic_StoreRelease(llc->config->channel_status_update_available, 1);
}

// abn_llc_Llc_Functions implementation
// ------------------------------------

void abn_llc_Llc_AddChannel(void* container_handle,
                            struct abn_llc_Channel* channel,
                            uint8_t channel_number,
                            uint8_t priority) {
  bool remotely_available;
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;

  ABN_ASSERT(llc != NULL);
  ABN_ASSERT(channel != NULL);

  abn_sync_Mutex_Lock(llc->config->mutex);

  ABN_ASSERT(llc->local_channels[channel_number] == NULL);

  llc->local_channels[channel_number] = channel;
  ++llc->number_locally_available_channels;
  abn_llc_ChannelPriorityList_SetChannelPriority(
      &llc->channels_with_tx_available_in_decreasing_priority,
      channel_number,
      priority);

  abn_llc_Status_SetChannelBit(&llc->local_channels_status,
                               channel_number,
                               ABN_LLC_CHANNEL_RX_FULL_BIT,
                               false);

  abn_llc_Llc_UpdatePendingAvailability(
      llc, channel_number, /*is_available*/ true);

  remotely_available =
      abn_llc_Status_GetChannelBit(&llc->remote_channels_status,
                                   channel_number,
                                   ABN_LLC_CHANNEL_AVAILABLE_BIT);

  if (remotely_available) {
    if (abn_llc_ChannelMask_Get(
            &llc->local_channel_close_pending_remote_close_ack,
            channel_number)) {
      // We are going to ignore it, since we know that we are going to
      // receive the "echoed" Close(), eventually followed by a new Open()
      // That is the moment when the channel becomes available again.
    } else {
      // if the channel was already opened remotely and there is no "echoed"
      // Close() pending, we notify the client that the channel is open
      channel->functions->handle_connection_callback(channel->channel_handle,
                                                     true);
    }
  }

  abn_sync_Mutex_Unlock(llc->config->mutex);

  // after signaling to MAC, that component will query LLC for TX data
  // Since at least one channel states has changes, a STATUS frame will be
  // sent over
  llc->mac->functions->signal_tx_data_available(llc->mac->mac_handle);
}

// This function can be called from different threads by the clients.
void abn_llc_Llc_RequestRemoveChannel(void* container_handle,
                                      uint8_t channel_number) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;

  ABN_ASSERT(llc != NULL);

  abn_sync_Mutex_Lock(llc->config->mutex);
  abn_llc_Llc_RequestRemoveChannelLocked(llc, channel_number);
  abn_sync_Mutex_Unlock(llc->config->mutex);
}

static void abn_llc_Llc_RemoveChannel(struct abn_llc_Llc* llc,
                                      uint8_t channel_number) {
  ABN_ASSERT(llc != NULL);

  abn_sync_Mutex_Lock(llc->config->mutex);

  if (llc->local_channels[channel_number] == NULL) {
    abn_sync_Mutex_Unlock(llc->config->mutex);
    return;
  }

  llc->local_channels[channel_number] = NULL;

  --llc->number_locally_available_channels;

  abn_llc_ChannelMask_Set(
      &llc->local_channels_with_tx_available_and_remote_rx_available,
      channel_number,
      false);

  abn_llc_ChannelMask_Set(
      &llc->local_channels_with_tx_available_and_remote_rx_unavailable,
      channel_number,
      false);

  abn_llc_Llc_UpdatePendingAvailability(
      llc, channel_number, /*is_available*/ false);

  abn_sync_Mutex_Unlock(llc->config->mutex);

  // after signaling to MAC, that component will query LLC for TX data
  // Since at least one channel state has changed, a STATUS frame will be
  // sent over
  llc->mac->functions->signal_tx_data_available(llc->mac->mac_handle);
}

void abn_llc_Llc_TxDataAvailable(void* container_handle,
                                 uint8_t channel_number) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;

  ABN_ASSERT(llc != NULL);

  abn_sync_Mutex_Lock(llc->config->mutex);

  if (llc->local_channels[channel_number] == NULL) {
    // This might happen because the channel was closed remotely
    // and there was a Write() call before the channel was
    // notified
    abn_sync_Mutex_Unlock(llc->config->mutex);
    ABN_LOG_WARN("autobahn: TXDataAvailable for a closed channel!");
    return;
  }

  if (!abn_llc_Status_GetChannelBit(&llc->remote_channels_status,
                                    channel_number,
                                    ABN_LLC_CHANNEL_RX_FULL_BIT)) {
    if (!abn_llc_ChannelMask_Get(
            &llc->local_channels_with_tx_available_and_remote_rx_available,
            channel_number)) {
      abn_llc_ChannelPriorityList_AddToList(
          &llc->channels_with_tx_available_in_decreasing_priority,
          channel_number);
      abn_sync_Atomic_StoreRelease(llc->config->channel_data_available, 1);
      abn_llc_ChannelMask_Set(
          &llc->local_channels_with_tx_available_and_remote_rx_available,
          channel_number,
          true);
    }
  } else {
    abn_llc_ChannelMask_Set(
        &llc->local_channels_with_tx_available_and_remote_rx_unavailable,
        channel_number,
        true);
  }

  abn_sync_Mutex_Unlock(llc->config->mutex);

  // after signaling to MAC, that component will query LLC for TX data
  // eventually, LLC will send over a DATA frame containing this channel data
  llc->mac->functions->signal_tx_data_available(llc->mac->mac_handle);
}

void abn_llc_Llc_RxSpaceAvailable(void* container_handle,
                                  uint8_t channel_number) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;

  ABN_ASSERT(llc != NULL);

  abn_sync_Mutex_Lock(llc->config->mutex);

  if (llc->local_channels[channel_number] == NULL) {
    // This might happen because the channel was closed remotely
    // and there was a Write() call before the channle was
    // notified
    ABN_LOG_WARN("autobahn: RxSpaceAvailable for a closed channel!");
    abn_sync_Mutex_Unlock(llc->config->mutex);
    return;
  }

  if (!abn_llc_Status_GetChannelBit(&llc->local_channels_status,
                                    channel_number,
                                    ABN_LLC_CHANNEL_RX_FULL_BIT)) {
    // If RX FULL bit is already not set, no need to do anything. This can
    // happen because there may have already been a pending RX FULL bit
    // update before this function was called, and the conditions for the
    // channel client to return true in get_rx_status were already met when
    // PrepareBuffers executed.
    abn_sync_Mutex_Unlock(llc->config->mutex);
    return;
  }

  abn_llc_StatusUpdate_SetChannelBit(
      &llc->pending_channel_rx_full_status_update, channel_number, true);

  abn_sync_Atomic_StoreRelease(llc->config->channel_status_update_available, 1);

  abn_sync_Mutex_Unlock(llc->config->mutex);

  // after signaling to MAC, that component will query LLC for TX data
  // Since at least one channel state has changed, a STATUS frame will be
  // sent over
  llc->mac->functions->signal_tx_data_available(llc->mac->mac_handle);
}

// This is called form the proxy thread
// It execute all the pending callback notifications
void abn_llc_Llc_ExecuteCallbacks(void* container_handle) {
  struct abn_llc_Llc_Node* node;
  struct abn_llc_Channel* channel;
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;
  bool remove_channel = false;

  ABN_ASSERT(llc != NULL);

  while (true) {
    abn_sync_Mutex_Lock(llc->config->mutex_callbacks);

    node = llc->pending_callbacks_head_node;
    if (node == NULL) {
      abn_sync_Mutex_Unlock(llc->config->mutex_callbacks);
      break;
    }

    llc->pending_callbacks_head_node = llc->pending_callbacks_head_node->next;
    if (llc->pending_callbacks_head_node == NULL) {
      llc->pending_callbacks_tail_node = NULL;
    }
    channel = node->channel;
    ABN_ASSERT(channel != NULL);

    remove_channel = abn_llc_ChannelMask_Get(&llc->remove_channel_pending_flags,
                                             node->channel_number);

    abn_llc_ChannelMask_Set(
        &llc->remove_channel_pending_flags, node->channel_number, false);
    abn_llc_ChannelMask_Set(
        &llc->callbacks_pending_nodes_flags, node->channel_number, false);

    abn_sync_Mutex_Unlock(llc->config->mutex_callbacks);

    if (remove_channel) {
      abn_llc_Llc_RemoveChannel(llc, node->channel_number);
    }

    channel->functions->execute_callbacks(channel->channel_handle);

    abn_sync_Mutex_Lock(llc->config->mutex_callbacks);
    node->next = llc->available_nodes_pool_head_node;
    llc->available_nodes_pool_head_node = node;
    abn_sync_Mutex_Unlock(llc->config->mutex_callbacks);
  }
}

// this is called from the channel context, when there is a new callback
// notification pending
void abn_llc_Llc_CallbackNotification(void* container_handle,
                                      uint8_t channel_number,
                                      struct abn_llc_Channel* channel) {
  struct abn_llc_Llc_Node* node;
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;

  ABN_ASSERT(llc != NULL);

  abn_sync_Mutex_Lock(llc->config->mutex_callbacks);

  if (!abn_llc_ChannelMask_Get(&llc->callbacks_pending_nodes_flags,
                               channel_number)) {
    ABN_ASSERT(llc->available_nodes_pool_head_node != NULL);
    node = llc->available_nodes_pool_head_node;
    llc->available_nodes_pool_head_node =
        llc->available_nodes_pool_head_node->next;

    node->channel = channel;
    node->channel_number = channel_number;
    node->next = NULL;

    if (llc->pending_callbacks_head_node == NULL) {
      llc->pending_callbacks_head_node = node;
      llc->pending_callbacks_tail_node = node;
    } else {
      llc->pending_callbacks_tail_node->next = node;
      llc->pending_callbacks_tail_node = node;
    }

    abn_llc_ChannelMask_Set(
        &llc->callbacks_pending_nodes_flags, channel_number, true);
  }

  abn_sync_Mutex_Unlock(llc->config->mutex_callbacks);

  llc->proxy->functions->trigger_event_notification(llc->proxy->proxy_handle);
}

static bool abn_llc_Llc_Pm_LlcIsIdle(struct abn_llc_Llc* llc) {
  if (abn_llc_Llc_IsTxDataAvailable(llc)) {
    return false;
  }

  abn_sync_Mutex_Lock(llc->config->mutex_callbacks);
  if (llc->pending_callbacks_head_node != NULL) {
    abn_sync_Mutex_Unlock(llc->config->mutex_callbacks);
    return false;
  }
  abn_sync_Mutex_Unlock(llc->config->mutex_callbacks);

  return true;
}

bool abn_llc_Llc_Pm_IsIdle(void* container_handle) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;

  ABN_ASSERT(llc != NULL);

  if (!abn_llc_Llc_Pm_LlcIsIdle(llc)) {
    return false;
  }

  return llc->mac_pm->functions->is_idle(llc->mac_pm->mac_handle);
}

void abn_llc_Llc_Pm_SetIdleCallback(void* container_handle,
                                    abn_llc_Pm_IdleCallback callback,
                                    void* context) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)container_handle;

  ABN_ASSERT(llc != NULL);
  // By design, we don't allow changing the IDLE callback more than once
  ABN_ASSERT(llc->idle_callback == NULL);

  llc->idle_callback_context = context;
  llc->idle_callback = callback;
}

// This is called when MAC changes its IDLE state
// If LLC itself is IDLE, we call the IDLE callback, if any was set
void abn_llc_Llc_Pm_MacIdleCallback(void* context,
                                    enum abn_mac_Pm_IdleState mac_idle_state) {
  struct abn_llc_Llc* llc = (struct abn_llc_Llc*)context;

  ABN_ASSERT(llc != NULL);

  if (llc->idle_callback == NULL) {
    return;
  }

  if (mac_idle_state == ABN_MAC_PM_STATE_ENTER_IDLE) {
    if (llc->idle_state_reported == ABN_LLC_PM_STATE_ENTER_IDLE) {
      return;
    }

    if (!abn_llc_Llc_Pm_LlcIsIdle(llc)) {
      return;
    }

    llc->idle_state_reported = ABN_LLC_PM_STATE_ENTER_IDLE;
  } else {
    if (llc->idle_state_reported == ABN_LLC_PM_STATE_EXIT_IDLE) {
      return;
    }

    llc->idle_state_reported = ABN_LLC_PM_STATE_EXIT_IDLE;
  }

  llc->idle_callback(llc->idle_callback_context, llc->idle_state_reported);
}
