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

#include "autobahn/llc/channel.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/queue.h"
#include "autobahn/sync/atomic.h"
#include "autobahn/sync/mutex.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

enum abn_llc_ChannelEvent {
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_READ_INITIATED = 0,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_READ_COMPLETED,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_WRITE_INITIATED,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_WRITE_COMPLETED,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_PREPARE_TX_FRAME,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_PROCESS_RX_FRAME,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_QUEUE_FULL_LOCALLY,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_SPACE_AVAILABLE_LOCALLY,
  ABN_LLC_APPLICATION_CHANNEL_CALLBACK_RX_QUEUE_FILLED,
};

enum abn_llc_Channel_ConnectionStatusType {
  ABN_LLC_CHANNEL_CONNECTION_STATUS_OPEN = 0,
  ABN_LLC_CHANNEL_CONNECTION_STATUS_CLOSE,
  ABN_LLC_CHANNEL_CONNECTION_STATUS_UNSET,
};

// A streaming Autobahn channel which connects via the LLC.
//
// TODO(b/316037763): Figure out what API extensions are required to efficiently
// support userspace interfaces.
struct abn_StreamingChannel;

// Callback invoked after requesting open and the channel succeeded or failed to
// open, local requests to close, or due to remote channel closures.
//
// Open is set to true if the channel opened.
//
// Note that the channel may unexpectedly close due to the remote end through
// the connection status callback. StreamingChannel public APIs may be
// called inside this callback.
typedef void (*abn_StreamingChannel_Client_HandleConnectionStatus)(
    void* client_handle, bool open);

// Callback invoked for the result of an enqueued read request.
//
// On read failure, success is set to false. Only on success will there be valid
// data in the buffer.
//
// Note that buffer_size_bytes is always set to the originally requested read
// size (buffer_size_bytes), regardless of success.
//
// The ownership of the buffer handed off by invoking Read is returned to the
// caller through this callback. The buffer pointer will always be the same as
// the one passed during read. StreamingChannel public APIs may be
// called inside this callback.
typedef void (*abn_StreamingChannel_Client_HandlReadResult)(
    void* client_handle,
    bool success,
    const uint8_t* buffer,
    size_t buffer_size_bytes);

// Callback invoked for the result of an enqueued write request.
//
// On write failure, success is set to false.
//
// The ownership of the data is handed of by invoking Write and is returned to
// the caller through this callback. StreamingChannel public APIs may be
// called inside this callback to avoid deadlocks.
typedef void (*abn_StreamingChannel_Client_HandleWriteResult)(
    void* client_handle,
    bool success,
    const uint8_t* data,
    size_t data_size_bytes);

typedef void (*abn_llc_Llc_ApplicationChannelCallback_Notification)(
    void* client_handle, enum abn_llc_ChannelEvent event, uint32_t data);

// Callbacks which are invoked on the Autobahn thread on behalf of the
// StreamingChannel's user.
//
// These callbacks are all permitted to use the public API of the
// StreamingChannel, ergo the channel implementation must be careful to release
// locks before invoking these callbacks to avoid recursive/reentrant mutex
// locking which is not supported.
struct abn_StreamingChannel_Client_Functions {
  abn_StreamingChannel_Client_HandleConnectionStatus handle_connection_status;
  abn_StreamingChannel_Client_HandlReadResult handle_read_result;
  abn_StreamingChannel_Client_HandleWriteResult handle_write_result;
};
struct abn_StreamingChannel_Client {
  const struct abn_StreamingChannel_Client_Functions* functions;
  void* client_handle;
};

// Precondition:
// - The channels' receive buffer must be greater than or equal to two Autobahn
//   LLC datagram.
void abn_StreamingChannel_Init(struct abn_StreamingChannel* channel);

// Closes the channel if it's open, meaning it may result in read result, write
// result, and connection status callbacks.
// Precondition:
// - the client should close the channel before calling Deinit()
void abn_StreamingChannel_Deinit(struct abn_StreamingChannel* channel);

// Requests to open the channel, wait for the connection status callback.
//
// You must either explicitly close a channel or receive a close connection
// status callback event before re-attempting to open a channel.
//
// Preconditions:
// - RX buffer is greater than or equal to one Autobahn LLC dagram.
void abn_StreamingChannel_Open(
    struct abn_StreamingChannel* channel,
    const struct abn_StreamingChannel_Client* client);

// Closes the channel, cancelling any pending operations meaning it may result
// in first a connection status callbacks, and second read result and
// write result callbacks if pending.
//
// This function will not return until the channel is closed.
//
// Note that this is idempotent, you can call it multiple times.
void abn_StreamingChannel_Close(struct abn_StreamingChannel* channel);

// Enqueues the data for transmission without blocking. The user cannot modify
// the buffer when Write() is called until the write operation is done and the
// write result callback is invoked.
//
// Precondition:
// - The data size must be greater than zero.
// - No write operation in progress.
//
// Returns true if the write was successfully enqueued, wait for the write
// result callback. False is returned if the channel is closed and ergo the
// request was rejected.
//
// TODO(b/315701772): Support multiple writes in parallel in order to permit
// coalescing of multiple writes within a single frame. Note that we'd also
// need to provide insight for the client as to the capcity and size of the
// transmit request queue.
bool abn_StreamingChannel_Write(struct abn_StreamingChannel* channel,
                                const uint8_t* data,
                                size_t data_size_bytes);

// Reads data into the provided buffer without blocking. The user cannot modify
// the buffer when Read() is called until the read operation is done and the
// read result callback is invoked.
//
// Precondition:
// - The buffer size must be greater than zero.
// - No read operation in progress.
//
// Returns true if the read request has been enqueued, wait for the read result
// callback. False is returned if the channel is closed and ergo the request was
// rejected.
bool abn_StreamingChannel_Read(struct abn_StreamingChannel* channel,
                               uint8_t* buffer,
                               size_t buffer_size_bytes);

void abn_llc_StreamingChannel_ExecuteCallbacks(void* channel_handle);

struct abn_StreamingChannel_Config {
  abn_Channel_Index index;

  abn_Channel_Priority priority;

  // Channel backpressure will be reported to the other side of the channel
  // whenever there is only sufficient space left for two Autobahn LLC
  // datagrams. This is because within the current change we may receive another
  // packet for the same channel and the status exchange for backpressure may
  // receive a second packet.
  // In other words if you do not want to cause back pressure to be reported
  // while handling a single response, you should size it to be greater than or
  // equal to two Autobahn LLC datagrams.
  uint8_t* rx_queue_buffer;
  size_t rx_queue_buffer_size;

  struct abn_llc_Llc_Interface* llc;

  abn_sync_Mutex* lock;

  abn_llc_Llc_ApplicationChannelCallback_Notification notification;
  void* notification_handle;
};

struct abn_StreamingChannel {
  const struct abn_StreamingChannel_Config* config;

  const struct abn_StreamingChannel_Client* client;

  struct abn_llc_Channel llc_client;

  abn_Queue rx_queue;

  const uint8_t* pending_write_data;
  // These two pending_* vars are valid only when pending_read_data is non-NULL
  size_t pending_write_data_size_bytes;
  size_t pending_write_data_bytes_sent;

  uint8_t* pending_read_data;
  // These two pending_* vars are valid only when pending_read_data is non-NULL
  size_t pending_read_data_size_bytes;
  size_t pending_read_data_bytes_received;

  // Channel is open both locally and remotely
  bool opened;

  bool pending_write_callback;
  bool pending_read_callback;
  enum abn_llc_Channel_ConnectionStatusType pending_connection_value;
};

ABN_EXTERN_C_END
