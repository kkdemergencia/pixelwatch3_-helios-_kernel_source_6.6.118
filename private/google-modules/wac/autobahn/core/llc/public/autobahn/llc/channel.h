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

#include "autobahn/preprocessor/util.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

enum abn_llc_ProcessRxDatagram_ReturnValue {
  ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_SUCCESS = 0,
  ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_RX_FULL,
  ABN_LLC_CHANNEL_PROCESS_RX_DATAGRAM_FAIL,
};

typedef uint8_t abn_Channel_Priority;

typedef uint8_t abn_Channel_Index;

// Gets the TX data to be sent.
// Preconditions:
// - datagram_out must be valid pointers
// - datagram_size_out must be a valid pointer
// - datagram_out size must be at least ABN_MAX_DATAGRAM_SIZE
// datagram_out contains the actual data size after return
// has_more_tx_datagrams is set to true if there is additional datagrams to send
// after this call
// Returns true if there is TX data to be sent, returns false otherwise.
typedef bool (*abn_llc_Channel_GetTxDatagram)(void* channel_handle,
                                              uint8_t* datagram_out,
                                              size_t* datagram_size_out,
                                              bool* has_more_tx_datagrams);

// Process RX data
// Preconditions:
// - datagram be valid pointers
// - datagram_size is the size of the data
// - the container storing the data should have enough available free space
// to store the new data based on the previous bool return.
//
// Return true if the channel can still receive data after processing
// the current data, return false otherwise.
typedef enum abn_llc_ProcessRxDatagram_ReturnValue (
    *abn_llc_Channel_ProcessRxDatagram)(void* channel_handle,
                                        uint8_t* datagram,
                                        size_t datagram_size);

// Callback invoked after requesting open and the channel succeeded or failed to
// open, local requests to close, or due to remote channel closures.
//
// Open is set to true if the channel opened.
//
// Note that the channel may unexpectedly close due to the remote end through
// the connection status callback.
typedef void (*abn_llc_Channel_HandleConnectionStatus)(void* channel_handle,
                                                       bool open);

// Process the pending callbacks notifications
typedef void (*abn_llc_Channel_ExecuteCallbacks)(void* channel_handle);

// Verifies whether the channel is ready to receive more data.
//
// Returns true if the channel can receive data, false otherwise.
typedef bool (*abn_llc_Channel_GetRxStatus)(void* channel_handle);

struct abn_llc_Channel_Functions {
  abn_llc_Channel_GetTxDatagram get_tx_datagram;
  abn_llc_Channel_ProcessRxDatagram process_rx_datagram;
  abn_llc_Channel_HandleConnectionStatus handle_connection_callback;
  abn_llc_Channel_ExecuteCallbacks execute_callbacks;
  abn_llc_Channel_GetRxStatus get_rx_status;
};

// abn_llc_Channel_Client instances are expected to be fully initialized and
// enabled before using them.
struct abn_llc_Channel {
  const struct abn_llc_Channel_Functions* functions;
  void* channel_handle;
};

ABN_EXTERN_C_END
