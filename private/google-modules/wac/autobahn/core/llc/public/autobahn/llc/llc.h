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

#include "autobahn/llc/channel_priority_list.h"
#include "autobahn/llc/frame.h"
#include "autobahn/llc/llc_pm.h"
#include "autobahn/llc/proxy_interface.h"
#include "autobahn/llc/status.h"
#include "autobahn/llc/status_update.h"
#include "autobahn/mac/client.h"
#include "autobahn/mac/mac.h"
#include "autobahn/mac/mac_pm.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/sync/atomic.h"
#include "autobahn/sync/mutex.h"

ABN_EXTERN_C_START

// Version of Autobahn LLC protocol
#define ABN_LLC_LLC_VERSION 1

#define ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW 2

#define SEQUENCE_ID_INITIAL_VALUE 0

enum abn_llc_ClientEvent {
  ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_SENT = 0,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_RECEIVED,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_FRAME_RECEIVED,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_VERSION,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_NACK_RECEIVED,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_EMPTY_SENT,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_STATUS_SENT,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_DATA_SENT,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_EMPTY_RECEIVED,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_STATUS_RECEIVED,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_DATA_RECEIVED,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_OUTGOING_VS_EXPECTED_SEQUENCE_ID_MISMATCH,
  ABN_LLC_APPLICATION_CALLBACK_EVENT_EXPECTED_VS_INCOMING_SEQUENCE_ID_MISMATCH,
};

enum abn_llc_PendingStatusChange {
  STATUS_CHANGE_NONE = 0,
  STATUS_CHANGE_AVAILABLE,
  STATUS_CHANGE_NOT_AVAILABLE,
};

typedef enum abn_llc_PendingStatusChange
    abn_llc_PendingStatusChangeChannels[ABN_LLC_NUM_CHANNELS];

// This is the implementation of
// abn_mac_Client->functions->is_tx_data_available()
bool abn_llc_Llc_IsTxDataAvailable(void* client_handle);

// This is the implementation of abn_mac_Client->functions->prepare_buffers()
void abn_llc_Llc_PrepareBuffers(void* client_handle,
                                struct abn_mac_Frame** tx_buffer_out,
                                struct abn_mac_Frame** rx_buffer_out);

// This is the implementation of abn_mac_Client->functions->process_rx_data()
// Preconditions:
// - After this is executed, IsTxDataAvailable() is going to be called again.
//   (as a result, there is no need to call mac->signal_tx_data_available() from
//    inside of this)
void abn_llc_Llc_ProcessRxData(void* client_handle,
                               struct abn_mac_Frame* buffer);

// This is the implementation of
// abn_mac_Client->functions->handle_failed_transaction()
void abn_llc_Llc_HandleTransactionFailure(void* client_handle);

typedef bool (*abn_llc_Llc_ApplicationCallback_Notification)(
    void* client_handle, enum abn_llc_ClientEvent);

struct abn_llc_Llc_ApplicationCallback_Functions {
  abn_llc_Llc_ApplicationCallback_Notification notification;
};

// Interface of the main LLC client
struct abn_llc_Llc_ApplicationCallback {
  const struct abn_llc_Llc_ApplicationCallback_Functions* functions;
  void* application_handle;
};

struct abn_llc_Llc_Node {
  uint8_t channel_number;
  struct abn_llc_Channel* channel;
  struct abn_llc_Llc_Node* next;
};

struct abn_llc_LLc_Config {
  // This is used to notify the upper layer about different events
  // for example INIT sent/received
  // Should we move this to the channel interface and notify the channels
  // directly?
  struct abn_llc_Llc_ApplicationCallback* client;
  abn_llc_Frame_IntegrityChecker integrity_checker;
  // this is used for protecting the local_channels container
  abn_sync_Mutex* mutex;
  // this is used for protecting the callback_channels container
  abn_sync_Mutex* mutex_callbacks;

  // We need 2 TX frames in order to cache the previous frame sent
  // in order to re-sent it when we received an ack = 1
  struct abn_llc_Frame* tx_frames_queue[ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW];
  struct abn_llc_Frame* rx_frame;

  // Internal Atomic object passed from upper layer
  // Set to 1 if there are changes in local_channels_status
  // As a result, STATUS frame will be sent next with local_channels_status
  // content
  // Instead of using a bool and locking inside abn_llc_Llc_IsTxDataAvailable(),
  // we optimize the code to use an atomic and avoid locking on ABN MAC thread
  // (abn_llc_Llc_IsTxDataAvailable call)
  abn_sync_Atomic* channel_status_update_available;

  // Internal Atomic object passed from upper layer
  // Set to 1 if there are channels with TX data available
  // (in other words, when priority_list is not empty)
  // Set to 0, when priority_list is empty
  // Instead of using a check on
  // channels_with_tx_available_in_decreasing_priority.number_channels_in_priority_list
  // which would require a lock, we use this Atomic flag
  abn_sync_Atomic* channel_data_available;

  // contains the pool of available nodes for pending
  // callback notification channels
  struct abn_llc_Llc_Node* head_pool_nodes;

  uint8_t reinit_window_empty_frame_count;
};

struct abn_llc_Llc {
  const struct abn_llc_LLc_Config* config;
  const struct abn_mac_Mac* mac;
  const struct abn_llc_Llc_Proxy_Client* proxy;
  const struct abn_mac_Pm* mac_pm;

  bool init_sent;
  bool init_received;

  // Set to true after a valid frame is received, false otherwise
  // If the previous frame was invalid, the next frame is ignored
  // and this is set to true
  // This value will be used in the next TX frame
  bool previous_rx_frame_valid;

  // Set to true after an invalid frame is received or
  // after a frame which is different from EMPTY is received
  // Set to false after a TX frame is sent back
  // When true, it means TX data is pending
  bool response_required;

  // For sequence ID mechanism:
  // This is the number of frames we need to retransmit (including the newest
  // one) Since ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW = 2, this can be maximum = 2,
  // but theoretically we support any value
  // For ACK/NACK mechanism:
  // Set to num_valid_frames_in_tx_frames_queue when a frame with ack = 0 is
  // received. As a result, the next num_valid_frames_in_tx_frames_queue frames
  // will be sent from the cached frames
  uint8_t number_pending_retransmissions;

  // Set to true when HandleFailedTransaction() is called
  // after PrepareBuffers() is called
  // Set to false after PrepareBuffers() is called
  bool last_transaction_failed;

  // This can be maximum two and used to initialize
  // number_pending_retransmissions only. This is used only in case we need to
  // retransmit a frame before having a history of two frames sent
  uint8_t number_valid_in_tx_frames_queue;

  // This is set  to true when we receive an ivalid frame
  // The reason for that is that we have to ignore the next frame, too
  bool ignore_next_rx_frame;

  // Set to true to override next transfer and send and empty frame even if
  // there are other data frames pending transmission.
  // Used during error recovery while waiting for retransmissions in order
  // not to break backpressure control.
  bool next_transfer_suppress_data_frame;

  // This is the current TX frame used to send data
  // The value of this can be 0 or 1
  int newest_frame_index_in_tx_frames_queue;

  // This reflects local channel client availability and rx backpressure
  // Guarded by LLC's mutex
  struct abn_llc_Status local_channels_status;

  // This is based on the last received STATUS frame
  struct abn_llc_Status remote_channels_status;

  // Set when there is a change in a channel rx status. In order to avoid
  // locking issues, user threads do not directly alter the RX full bit.
  // Instead, the corresponding bit in pending_channel_rx_full_status_update
  // is set and the RX FULL state is updated during PrepareBuffers().
  struct abn_llc_StatusUpdate pending_channel_rx_full_status_update;

  // contains the number of channel which are open locally
  uint16_t number_locally_available_channels;

  // hold a pointer to the channel for those which are open locally
  // hold NULL for those which are not open
  struct abn_llc_Channel* local_channels[ABN_LLC_NUM_CHANNELS];

  // List of local channels with TX data and RX_FULL bit set to 0
  // The channels are ordered in decreasing priority order
  struct abn_llc_ChannelPriorityList
      channels_with_tx_available_in_decreasing_priority;

  // This is the current index in prioritized_channels_with_tx_available.
  // The data is sent from the channel with maxim priority.
  // If there are more channels with maxim priority, we select a channel based
  // on round robin algorith.
  uint8_t current_channel_in_priority_list_index;

  // true when PrepareBuffers() is called
  // false when ProcessRxdata() is called
  bool prepared_buffers;

  enum abn_llc_FrameType last_sent_frame_type;

  struct abn_llc_Llc_Node* pending_callbacks_head_node;
  struct abn_llc_Llc_Node* pending_callbacks_tail_node;

  struct abn_llc_Llc_Node* available_nodes_pool_head_node;

  // this should be one bit per node
  struct abn_llc_ChannelMask callbacks_pending_nodes_flags;

  // this should be one bit per node
  struct abn_llc_ChannelMask remove_channel_pending_flags;

  // local channels with TX data to send but remote RX_BIT_FULL bit set to 0
  // this is in sync with channels_with_tx_available_in_decreasing_priority
  struct abn_llc_ChannelMask
      local_channels_with_tx_available_and_remote_rx_available;

  // local channels with TX data to send but remote RX_BIT_FULL bit set to 1
  // when RX_BIT_FULL bit is set to 0 (when STATUS FRAME IS RECEIVED)
  // that channel is moved to channels_with_tx_available_in_decreasing_priority
  struct abn_llc_ChannelMask
      local_channels_with_tx_available_and_remote_rx_unavailable;

  // The only time LLC can go from non-IDLE to IDLE is after
  // ProcessRxData is called.
  // However, in the current implementation, we know that at that time
  // MAC is not in IDLE (since it's the middle of a transaction).
  // As a result, it doesn't make sense to check if we can call the
  // callback there
  // So we should rely on MAC to go to IDLE and only on that callback
  // to check if LLC is in IDLE and eventually call the client callback
  // For performance reasons, we don't allow this callback to be set
  // more than once
  abn_llc_Pm_IdleCallback idle_callback;
  void* idle_callback_context;

  enum abn_llc_Pm_IdleState idle_state_reported;

  // TODO b/412943683: Remove this member. Temporarily used to insert empty
  // frames between reinit attempts.
  uint8_t empty_frame_count;

  // true when both sides support sequence ID feature
  // This is negotiated when INIT frame is sent/received
  bool sequence_id_enabled;

  // Expected Sequence ID for the incoming frame
  // Set by the sender
  uint8_t expected_incoming_flow_sequence_id;

  // A double buffer for pending channel availability status changes.
  // This ensures that rapid open/close/open sequences result in distinct
  // STATUS frames being sent.
  abn_llc_PendingStatusChangeChannels pending_availability_statuses[2];

  // Index into the pending_availability_status double buffer. 0 or 1.
  uint8_t current_pending_availability_index;

  // Counters showing when there are changes in pending_availability_statuses[]
  // A value greater than 0 means there are changes
  uint16_t pending_status_updates[2];

  // Set to true when a local channel close is initiated. Cleared when a
  // corresponding "unavailable" status is received from the remote peer. This
  // prevents a close-ack-close feedback loop.
  struct abn_llc_ChannelMask local_channel_close_pending_remote_close_ack;
};

// Initialize LLC
// This should be called before accesing any other API
void abn_llc_Llc_Init(struct abn_llc_Llc* llc,
                      const struct abn_llc_LLc_Config* config,
                      const struct abn_mac_Mac* mac,
                      const struct abn_llc_Llc_Proxy_Client* proxy,
                      const struct abn_mac_Pm* mac_pm);

// Re-initialize LLC without holding the lock
// After calling this API the internal state of LLC
// is similar with the one after calling Init(),
// except for:
// - list of opened channels is preserved
// - for pending read/write the callbacks are called
// - the connections are closed, client should call Open()
void abn_llc_Llc_ReInit(struct abn_llc_Llc* llc);

// Implementation of abn_llc_Llc_Functions's add_channel
// (abn_llc_Llc_RegisterChannel).
void abn_llc_Llc_AddChannel(void* container_handle,
                            struct abn_llc_Channel* channel,
                            uint8_t channel_number,
                            uint8_t priority);

// Implementation of abn_llc_Llc_Functions's remove_channel
// (abn_llc_Llc_UnregisterChannel).
void abn_llc_Llc_RequestRemoveChannel(void* container_handle,
                                      uint8_t channel_number);

// Implementation of abn_llc_Llc_Functions's signal_has_tx_data
// (abn_llc_Llc_SignalTxDataAvailable).
void abn_llc_Llc_TxDataAvailable(void* container_handle,
                                 uint8_t channel_number);

// Implementation of abn_llc_Llc_Functions's signal_rx_space_available
// (abn_llc_Llc_SignalRxSpaceAvailable).
void abn_llc_Llc_RxSpaceAvailable(void* container_handle,
                                  uint8_t channel_number);

// Execute teh pending callback notifications
// from the Proxy thread
void abn_llc_Llc_ExecuteCallbacks(void* container_handle);

// Signal the fact that there is a pending callback notification
void abn_llc_Llc_CallbackNotification(void* container_handle,
                                      uint8_t channel_numbe,
                                      struct abn_llc_Channel* channel);

bool abn_llc_Llc_Pm_IsIdle(void* container_handle);

void abn_llc_Llc_Pm_SetIdleCallback(void* container_handle,
                                    abn_llc_Pm_IdleCallback callback,
                                    void* context);

void abn_llc_Llc_Pm_MacIdleCallback(void* context,
                                    enum abn_mac_Pm_IdleState mac_idle_state);

ABN_EXTERN_C_END

#ifdef __cplusplus

namespace abn::llc {

inline constexpr abn_llc_Llc_Functions kLlcFunctionTable = {
    .add_channel = abn_llc_Llc_AddChannel,
    .remove_channel = abn_llc_Llc_RequestRemoveChannel,
    .signal_has_tx_data = abn_llc_Llc_TxDataAvailable,
    .signal_rx_space_available = abn_llc_Llc_RxSpaceAvailable,
    .signal_callback_notification = abn_llc_Llc_CallbackNotification,
};

inline constexpr abn_mac_Client_Functions kMacClientFunctionTable = {
    .is_tx_data_available = abn_llc_Llc_IsTxDataAvailable,
    .prepare_buffers = abn_llc_Llc_PrepareBuffers,
    .process_rx_data = abn_llc_Llc_ProcessRxData,
    .handle_transaction_failure = abn_llc_Llc_HandleTransactionFailure};
}  // namespace abn::llc

inline constexpr abn_llc_Pm_Functions kLlcFunctionTable = {
    .is_idle = abn_llc_Llc_Pm_IsIdle,
    .set_idle_callback = abn_llc_Llc_Pm_SetIdleCallback};

#endif  // defined(__cplusplus)
