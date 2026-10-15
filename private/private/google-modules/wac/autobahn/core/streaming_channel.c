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

#include "autobahn/streaming_channel.h"

#include "autobahn/assert/assert.h"
#include "autobahn/autobahn.h"
#include "autobahn/llc/channel.h"
#include "autobahn/llc/frame.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/log/log.h"
#include "autobahn/string.h"

static bool abn_StreamingChannel_GetTxDatagram(void* channel_handle,
                                               uint8_t* datagram_out,
                                               size_t* datagram_size_out,
                                               bool* has_additional_data_out) {
  struct abn_StreamingChannel* channel =
      (struct abn_StreamingChannel*)channel_handle;

  ABN_ASSERT(channel != NULL);
  ABN_ASSERT(datagram_out != NULL);
  ABN_ASSERT(datagram_size_out != NULL);

  abn_sync_Mutex_Lock(channel->config->lock);

  if (channel->pending_write_data == NULL) {
    *datagram_size_out = 0;
    abn_sync_Mutex_Unlock(channel->config->lock);
    *has_additional_data_out = false;
    return false;
  }

  *datagram_size_out = channel->pending_write_data_size_bytes -
                       channel->pending_write_data_bytes_sent;
  if (*datagram_size_out > ABN_MAX_DATAGRAM_SIZE) {
    *datagram_size_out = ABN_MAX_DATAGRAM_SIZE;
  }
  memcpy(datagram_out,
         channel->pending_write_data + channel->pending_write_data_bytes_sent,
         *datagram_size_out);
  channel->pending_write_data_bytes_sent += *datagram_size_out;

  if (channel->config->notification != NULL) {
    channel->config->notification(
        channel->config->notification_handle,
        ABN_LLC_APPLICATION_CHANNEL_CALLBACK_PREPARE_TX_FRAME,
        (uint32_t)*datagram_size_out);
  }

  if (channel->pending_write_data_bytes_sent <
      channel->pending_write_data_size_bytes) {
    abn_sync_Mutex_Unlock(channel->config->lock);
    *has_additional_data_out = true;
    return true;
  }

  channel->pending_write_callback = true;

  channel->config->llc->functions->signal_callback_notification(
      channel->config->llc->llc_handle,
      channel->config->index,
      &channel->llc_client);

  if (channel->config->notification != NULL) {
    channel->config->notification(
        channel->config->notification_handle,
        ABN_LLC_APPLICATION_CHANNEL_CALLBACK_WRITE_COMPLETED,
        0);
  }

  abn_sync_Mutex_Unlock(channel->config->lock);

  *has_additional_data_out = false;

  return true;
}

inline static bool abn_StreamingChannel_QueueHasEnoughSpace(
    struct abn_StreamingChannel* channel) {
  size_t available_size = abn_Queue_Capacity(&channel->rx_queue) -
                          abn_Queue_Size(&channel->rx_queue);

  // Lets take some example for better understanding why (available_size >
  // DATAGRAM_SIZE) is not enough MAX_DATAGRAM_SIZE = 256 available_size = 500
  // If we return true here and receive on the next transaction 256 bytes
  // available_size = 500 - 256 = 244
  // Now we want to return false on the next transaction but receive again 256
  // bytes we can't store them since there are only 244 bytes available If we
  // use (available_size >= 2*DATAGRAM_SIZE) check for the same example we
  // return false when available_size = 500 so even if we receive 256 bytes on
  // that transaction we can still store them, available_size = 244 but we
  // already inform the other side that we can't receive anymore data
  return available_size >= 2 * ABN_MAX_DATAGRAM_SIZE;
}

static enum abn_llc_ProcessRxDatagram_ReturnValue
abn_StreamingChannel_ProcessRxDatagram(void* channel_handle,
                                       uint8_t* datagram,
                                       size_t datagram_size) {
  size_t remaining_read_size, adding_data_size;
  bool queue_has_enough_space = true;
  struct abn_StreamingChannel* channel =
      (struct abn_StreamingChannel*)channel_handle;
  ABN_ASSERT(channel != NULL);

  if (channel->config->notification != NULL) {
    channel->config->notification(
        channel->config->notification_handle,
        ABN_LLC_APPLICATION_CHANNEL_CALLBACK_PROCESS_RX_FRAME,
        (uint32_t)datagram_size);
  }

  abn_sync_Mutex_Lock(channel->config->lock);

  // When a read is complete, we signal a callback notification and wait for
  // the callback to be called, so pending_read_data is not cleared
  // immediately. Handle receiving more data before the callback executes and
  // clears pending_read_data by also checking whether all pending bytes have
  // already been received
  // (pending_read_data_bytes_received == pending_read_data_size_bytes).
  if (channel->pending_read_data == NULL ||
      (channel->pending_read_data_bytes_received ==
       channel->pending_read_data_size_bytes)) {
    // If flow control is working as expected, we should never end up in a
    // situation where abn_Queue_Push fails. However, since this can happen
    // if the other side is misbehaving, handle it by logging the error.
    // Returning false here will also trigger a STATUS frame transmission.
    if (!abn_Queue_Push(&channel->rx_queue, datagram, datagram_size)) {
      abn_sync_Mutex_Unlock(channel->config->lock);
      ABN_LOG_CRITICAL(
          "Received data when queue is full. Data loss, closing channel!");
      return ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_FAIL;
    }
    queue_has_enough_space = abn_StreamingChannel_QueueHasEnoughSpace(channel);
    const size_t queue_size = abn_Queue_Size(&channel->rx_queue);
    abn_sync_Mutex_Unlock(channel->config->lock);
    if (channel->config->notification != NULL) {
      if (!queue_has_enough_space) {
        channel->config->notification(
            channel->config->notification_handle,
            ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_QUEUE_FULL_LOCALLY,
            0);
      }
      channel->config->notification(
          channel->config->notification_handle,
          ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_QUEUE_FILLED,
          queue_size);
    }
    return queue_has_enough_space ? ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_SUCCESS
                                  : ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_RX_FULL;
  }

  remaining_read_size = channel->pending_read_data_size_bytes -
                        channel->pending_read_data_bytes_received;
  if (remaining_read_size > datagram_size) {
    adding_data_size = datagram_size;
  } else {
    adding_data_size = remaining_read_size;
  }
  memcpy(channel->pending_read_data + channel->pending_read_data_bytes_received,
         datagram,
         adding_data_size);
  channel->pending_read_data_bytes_received += adding_data_size;
  if (channel->pending_read_data_bytes_received >=
      channel->pending_read_data_size_bytes) {
    remaining_read_size = datagram_size - adding_data_size;

    // If flow control is working as expected, we should never end up in a
    // situation where abn_Queue_Push fails. However, this can happen
    // if the other side is misbehaving.
    // The link is no longer reliable at this point, so trigger a channel
    // disconnect.
    if (!abn_Queue_Push(&channel->rx_queue,
                        datagram + adding_data_size,
                        remaining_read_size)) {
      abn_sync_Mutex_Unlock(channel->config->lock);
      ABN_LOG_CRITICAL(
          "Received data when queue is full. Data loss, closing channel!");
      return ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_FAIL;
    }
    queue_has_enough_space = abn_StreamingChannel_QueueHasEnoughSpace(channel);
    if (channel->config->notification != NULL) {
      channel->config->notification(
          channel->config->notification_handle,
          ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_QUEUE_FILLED,
          abn_Queue_Size(&channel->rx_queue));
    }
  }

  if (channel->pending_read_data != NULL) {
    channel->pending_read_callback = true;

    channel->config->llc->functions->signal_callback_notification(
        channel->config->llc->llc_handle,
        channel->config->index,
        &channel->llc_client);
  }

  if (channel->config->notification != NULL) {
    channel->config->notification(
        channel->config->notification_handle,
        ABN_LLC_APPLICATION_CHANNEL_CALLBACK_READ_COMPLETED,
        0);
  }

  abn_sync_Mutex_Unlock(channel->config->lock);

  return queue_has_enough_space ? ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_SUCCESS
                                : ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_RX_FULL;
}

static void abn_llc_StreamingChannel_AddPendingConnectionValue(
    struct abn_StreamingChannel* channel, bool is_open) {
  switch (channel->pending_connection_value) {
    case ABN_LLC_CHANNEL_CONNECTION_STATUS_UNSET:
      channel->pending_connection_value =
          is_open ? ABN_LLC_CHANNEL_CONNECTION_STATUS_OPEN
                  : ABN_LLC_CHANNEL_CONNECTION_STATUS_CLOSE;
      break;
    case ABN_LLC_CHANNEL_CONNECTION_STATUS_OPEN:
      if (!is_open) {
        // This is the open->close case
        // We should just report the drop the pending open and just handle the
        // close
        channel->pending_connection_value =
            ABN_LLC_CHANNEL_CONNECTION_STATUS_CLOSE;
      } else {
        ABN_LOG_ERROR(
            "StreamingChannel: Received open connection"
            " while being open %d",
            channel->config->index);
      }
      break;
    case ABN_LLC_CHANNEL_CONNECTION_STATUS_CLOSE:
      if (is_open) {
        // This is the close->open case
        // We should ignore open and report the close
        // Eventually the client is going to open the channel again
      } else {
        ABN_LOG_ERROR(
            "StreamingChannel: Received close connection"
            " while being closed %d",
            channel->config->index);
      }
      break;
  }
}

// This API going to be called from LLC, in the following cases:
// 1. the channel was opened remotely (after it was already opened locally)
// 2. the channel was opened locally (after it was already opened remotely)
// 3. we got an INIT frame form the other side, after it was already opened
//    locally
// 4. the channel was closed remotely, after it was already opened locally
// We don't get the call when the channel is closed locally
void abn_llc_StreamingChannel_HandleConnectionStatus(void* channel_handle,
                                                     bool open) {
  struct abn_StreamingChannel* channel =
      (struct abn_StreamingChannel*)channel_handle;
  ABN_ASSERT(channel != NULL);

  abn_sync_Mutex_Lock(channel->config->lock);

  abn_llc_StreamingChannel_AddPendingConnectionValue(channel, open);

  channel->config->llc->functions->signal_callback_notification(
      channel->config->llc->llc_handle,
      channel->config->index,
      &channel->llc_client);

  abn_sync_Mutex_Unlock(channel->config->lock);
}

// This should be called when channel->config->lock is locked
static void abn_llc_StreamingChannel_SetConnectionStatus(
    struct abn_StreamingChannel* channel, bool open) {
  if (channel->opened && open) {
    // Based on the current implementation (see AddPendingConnectionValue()),
    // it doesn't make sense to enter this path. We log the error,
    // most likely something went wrong
    ABN_LOG_ERROR(
        "StreamingChannel error: a streaming channel "
        "received an open callback while being already open");
    return;
  } else if (!channel->opened && !open) {
    // Based on the current implementation (see AddPendingConnectionValue()),
    // it doesn't make sense to enter this path. We log the error,
    // most likely something went wrong
    ABN_LOG_ERROR(
        "StreamingChannel error: a streaming channel "
        "received a close callback while being already closed");
    return;
  }

  channel->opened = open;
}

void abn_llc_StreamingChannel_ExecuteCallbacks(void* channel_handle) {
  const uint8_t* pending_write_data;
  const uint8_t* pending_read_data;
  size_t pending_write_data_size_bytes;
  size_t pending_read_data_size_bytes;
  bool pending_connection_status;
  bool pending_write_callback;
  bool pending_read_callback;
  bool pending_connection_callback;
  struct abn_StreamingChannel* channel =
      (struct abn_StreamingChannel*)channel_handle;
  ABN_ASSERT(channel != NULL);

  // Lock to protect pending callback variables. Callbacks are called
  // after releasing the lock to avoid deadlocks.
  abn_sync_Mutex_Lock(channel->config->lock);

  // Capture the pending states and clear them within the lock.
  pending_write_callback = channel->pending_write_callback;
  pending_write_data = channel->pending_write_data;
  pending_write_data_size_bytes = channel->pending_write_data_size_bytes;

  pending_read_callback = channel->pending_read_callback;
  pending_read_data = channel->pending_read_data;
  pending_read_data_size_bytes = channel->pending_read_data_size_bytes;

  pending_connection_callback = (channel->pending_connection_value !=
                                 ABN_LLC_CHANNEL_CONNECTION_STATUS_UNSET);
  pending_connection_status = (channel->pending_connection_value ==
                               ABN_LLC_CHANNEL_CONNECTION_STATUS_OPEN);

  if (pending_write_callback) {
    channel->pending_write_callback = false;
    channel->pending_write_data = NULL;
  }

  if (pending_read_callback) {
    channel->pending_read_callback = false;
    channel->pending_read_data = NULL;
  }

  channel->pending_connection_value = ABN_LLC_CHANNEL_CONNECTION_STATUS_UNSET;

  if (pending_connection_callback) {
    abn_llc_StreamingChannel_SetConnectionStatus(channel,
                                                 pending_connection_status);
  }

  abn_sync_Mutex_Unlock(channel->config->lock);

  // Execute callbacks outside the lock.
  if (pending_write_callback) {
    channel->client->functions->handle_write_result(
        channel->client->client_handle,
        true,
        pending_write_data,
        pending_write_data_size_bytes);
    pending_write_data = NULL;
  }

  if (pending_read_callback) {
    channel->client->functions->handle_read_result(
        channel->client->client_handle,
        true,
        pending_read_data,
        pending_read_data_size_bytes);
    pending_read_data = NULL;
  }

  if (pending_connection_callback) {
    channel->client->functions->handle_connection_status(
        channel->client->client_handle, pending_connection_status);

    if (pending_connection_status) {
      return;
    }
    // Handle potential remaining pending read/write data requests if
    // channel is closed.
    if (pending_write_data != NULL) {
      channel->client->functions->handle_write_result(
          channel->client->client_handle,
          false,
          pending_write_data,
          pending_write_data_size_bytes);
    }
    if (pending_read_data != NULL) {
      channel->client->functions->handle_read_result(
          channel->client->client_handle,
          false,
          pending_read_data,
          pending_read_data_size_bytes);
    }
  }
}

bool abn_llc_StreamingChannel_GetRxStatus(void* channel_handle) {
  struct abn_StreamingChannel* channel =
      (struct abn_StreamingChannel*)channel_handle;
  bool queue_has_enough_space;

  ABN_ASSERT(channel != NULL);

  abn_sync_Mutex_Lock(channel->config->lock);
  queue_has_enough_space = abn_StreamingChannel_QueueHasEnoughSpace(channel);
  abn_sync_Mutex_Unlock(channel->config->lock);

  return queue_has_enough_space;
}

static const struct abn_llc_Channel_Functions channel_client_functions = {
    .get_tx_datagram = abn_StreamingChannel_GetTxDatagram,
    .process_rx_datagram = abn_StreamingChannel_ProcessRxDatagram,
    .handle_connection_callback =
        abn_llc_StreamingChannel_HandleConnectionStatus,
    .execute_callbacks = abn_llc_StreamingChannel_ExecuteCallbacks,
    .get_rx_status = abn_llc_StreamingChannel_GetRxStatus};

void abn_StreamingChannel_Init(struct abn_StreamingChannel* channel) {
  ABN_ASSERT(channel != NULL);
  ABN_ASSERT(channel->config != NULL);

  channel->llc_client.channel_handle = channel;
  channel->llc_client.functions = &channel_client_functions;

  abn_Queue_Init(&channel->rx_queue,
                 channel->config->rx_queue_buffer,
                 channel->config->rx_queue_buffer_size);

  channel->pending_write_data = NULL;
  channel->pending_write_data_size_bytes = 0;
  channel->pending_write_data_bytes_sent = 0;

  channel->pending_read_data = NULL;
  channel->pending_read_data_size_bytes = 0;
  channel->pending_read_data_bytes_received = 0;

  channel->opened = false;

  channel->pending_write_callback = false;
  channel->pending_read_callback = false;
  channel->pending_connection_value = ABN_LLC_CHANNEL_CONNECTION_STATUS_UNSET;
}

void abn_StreamingChannel_Deinit(struct abn_StreamingChannel* channel) {
  ABN_ASSERT(channel != NULL);
}

// If the channel was already opened locally, we are going to assert inside
// add_channel
void abn_StreamingChannel_Open(
    struct abn_StreamingChannel* channel,
    const struct abn_StreamingChannel_Client* client) {
  ABN_ASSERT(channel != NULL);
  ABN_ASSERT(client != NULL);

  abn_sync_Mutex_Lock(channel->config->lock);

  channel->client = client;
  channel->pending_write_data = NULL;
  channel->pending_read_data = NULL;

  // There may be stale data from a previous session.
  abn_Queue_Init(&channel->rx_queue,
                 channel->config->rx_queue_buffer,
                 channel->config->rx_queue_buffer_size);

  abn_sync_Mutex_Unlock(channel->config->lock);

  // if the channel was opened only remotely, it's add_channel responsability to
  // call abn_llc_StreamingChannel_HandleConnectionStatus(true)
  channel->config->llc->functions->add_channel(channel->config->llc->llc_handle,
                                               &channel->llc_client,
                                               channel->config->index,
                                               channel->config->priority);
}

// This can be called multiple times, remove_channel should support it.
// Also called internally in abn_StreamingChannel_ProcessRxDatagram.
void abn_StreamingChannel_Close(struct abn_StreamingChannel* channel) {
  ABN_ASSERT(channel != NULL);

  // if the channel was opened, it's remove_channel responsability to call
  // abn_llc_StreamingChannel_HandleConnectionStatus(false)
  channel->config->llc->functions->remove_channel(
      channel->config->llc->llc_handle, channel->config->index);
}

bool abn_StreamingChannel_Write(struct abn_StreamingChannel* channel,
                                const uint8_t* data,
                                size_t data_size_bytes) {
  ABN_ASSERT(channel != NULL);
  ABN_ASSERT(data != NULL);
  ABN_ASSERT(data_size_bytes != 0);

  if (channel->config->notification != NULL) {
    channel->config->notification(
        channel->config->notification_handle,
        ABN_LLC_APPLICATION_CHANNEL_CALLBACK_WRITE_INITIATED,
        0);
  }

  abn_sync_Mutex_Lock(channel->config->lock);

  if (channel->pending_write_data != NULL) {
    // This might happen because of a client issue or
    // because of an internal state machine bug.
    // We should investigate and fix this error.
#if ABN_DEBUG_ASSERT_ENABLED
    ABN_LOG_FATAL(
        "StreamingChannel error: a new Write was called "
        "while an existing Write is in progress");
#else
    ABN_LOG_CRITICAL(
        "StreamingChannel error: a new Write was called "
        "while an existing Write is in progress. "
        "The channel is going to be closed.");
    abn_sync_Mutex_Unlock(channel->config->lock);
    // If we continue and execute this path, we can close
    // the channel here so the client will get the notification
    // and try to open it again.
    abn_StreamingChannel_Close(channel);
    return false;
#endif  // ABN_DEBUG_ASSERT_ENABLED
  }

  if (!channel->opened) {
    abn_sync_Mutex_Unlock(channel->config->lock);
    return false;
  }

  channel->pending_write_data = data;
  channel->pending_write_data_size_bytes = data_size_bytes;
  channel->pending_write_data_bytes_sent = 0;

  abn_sync_Mutex_Unlock(channel->config->lock);

  channel->config->llc->functions->signal_has_tx_data(
      channel->config->llc->llc_handle, channel->config->index);

  return true;
}

bool abn_StreamingChannel_Read(struct abn_StreamingChannel* channel,
                               uint8_t* buffer,
                               size_t buffer_size_bytes) {
  size_t queue_data_size, queue_data_read_size,
      remaining_read_size_bytes = buffer_size_bytes;
  bool queue_has_enough_space_before, report_signal_rx_space_available = false;

  ABN_ASSERT(channel != NULL);
  ABN_ASSERT(buffer != NULL);
  ABN_ASSERT(buffer_size_bytes != 0);

  if (channel->config->notification != NULL) {
    channel->config->notification(
        channel->config->notification_handle,
        ABN_LLC_APPLICATION_CHANNEL_CALLBACK_READ_INITIATED,
        0);
  }

  abn_sync_Mutex_Lock(channel->config->lock);

  if (channel->pending_read_data != NULL) {
    // This might happen because of a client issue or
    // because of an internal state machine bug.
    // We should investigate and fix this error.
#if ABN_DEBUG_ASSERT_ENABLED
    ABN_LOG_FATAL(
        "StreamingChannel error: a new Read was called "
        "while an existing Read is in progress");
#else
    ABN_LOG_CRITICAL(
        "StreamingChannel error: a new Read was called "
        "while an existing Read is in progress. "
        "The channel is going to be closed.");
    abn_sync_Mutex_Unlock(channel->config->lock);
    // If we continue and execute this path, we can close
    // the channel here so the client will get the notification
    // and try to open it again.
    abn_StreamingChannel_Close(channel);
    return false;
#endif  // ABN_DEBUG_ASSERT_ENABLED
  }

  if (!channel->opened) {
    abn_sync_Mutex_Unlock(channel->config->lock);
    return false;
  }

  queue_data_size = abn_Queue_Size(&channel->rx_queue);
  if (queue_data_size > 0) {
    queue_has_enough_space_before =
        abn_StreamingChannel_QueueHasEnoughSpace(channel);
    if (queue_data_size > buffer_size_bytes) {
      queue_data_read_size = buffer_size_bytes;
    } else {
      queue_data_read_size = queue_data_size;
    }
    abn_Queue_Pop(&channel->rx_queue, buffer, queue_data_read_size);
    remaining_read_size_bytes -= queue_data_read_size;

    // We signal LLC that RX space is available only when there was no space
    // before consuming the data and space became available after consuming it
    report_signal_rx_space_available =
        !queue_has_enough_space_before &&
        abn_StreamingChannel_QueueHasEnoughSpace(channel);

    if (remaining_read_size_bytes == 0) {
      channel->pending_read_callback = true;
      channel->pending_read_data = buffer;
      channel->pending_read_data_size_bytes = buffer_size_bytes;
      channel->pending_read_data_bytes_received = buffer_size_bytes;

      channel->config->llc->functions->signal_callback_notification(
          channel->config->llc->llc_handle,
          channel->config->index,
          &channel->llc_client);

      abn_sync_Mutex_Unlock(channel->config->lock);

      if (report_signal_rx_space_available) {
        channel->config->llc->functions->signal_rx_space_available(
            channel->config->llc->llc_handle, channel->config->index);
        if (channel->config->notification != NULL) {
          channel->config->notification(
              channel->config->notification_handle,
              ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_SPACE_AVAILABLE_LOCALLY,
              0);
        }
      }

      if (channel->config->notification != NULL) {
        channel->config->notification(
            channel->config->notification_handle,
            ABN_LLC_APPLICATION_CHANNEL_CALLBACK_READ_COMPLETED,
            0);
      }

      return true;
    }
  }

  channel->pending_read_data = buffer;
  channel->pending_read_data_size_bytes = buffer_size_bytes;
  channel->pending_read_data_bytes_received =
      buffer_size_bytes - remaining_read_size_bytes;

  abn_sync_Mutex_Unlock(channel->config->lock);

  if (report_signal_rx_space_available && remaining_read_size_bytes != 0) {
    channel->config->llc->functions->signal_rx_space_available(
        channel->config->llc->llc_handle, channel->config->index);
    if (channel->config->notification != NULL) {
      channel->config->notification(
          channel->config->notification_handle,
          ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_SPACE_AVAILABLE_LOCALLY,
          0);
    }
  }

  return true;
}
