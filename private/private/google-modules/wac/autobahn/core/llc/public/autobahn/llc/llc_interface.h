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
#include "autobahn/preprocessor/util.h"

ABN_EXTERN_C_START

#define ABN_LLC_NUM_CHANNELS 256

// Register a channel to the container.
//
// The implementation of this API will send the STATUS frame
// to notify the other side that the channel is available and
// it can receive data.
//
// The priority does not have to be unique, round robin is used across
// the same priority value. Higher numbers are higher priority.
//
// Preconditions:
// - channel must be a valid pointer
// - no other channel was already registered on the same channel_number
typedef void (*abn_llc_Llc_RegisterChannel)(void* container_handle,
                                            struct abn_llc_Channel* channel,
                                            uint8_t channel_number,
                                            uint8_t priority);

// Unregister a channel from the container.
// This should be idempotent, you can call unregister multiple times.
// The implementation of this API will send the STATUS frame
// to notify the other side that the channel is unavailable
typedef void (*abn_llc_Llc_UnregisterChannel)(void* container_handle,
                                              uint8_t channel_number);

// Signal to the container that the channel has data to sent.
// Preconditions:
// - the channel was added before at the same channel_number
typedef void (*abn_llc_Llc_SignalTxDataAvailable)(void* container_handle,
                                                  uint8_t channel_number);

// Signal to the container that the channel can receive data
// Preconditions:
// - the channel was added before at the same channel_number
typedef void (*abn_llc_Llc_SignalRxSpaceAvailable)(void* container_handle,
                                                   uint8_t channel_number);

// Signal the fact that there are pending callback notifications which need
// to be executed
typedef void (*abn_llc_Llc_SignalCallbackNotification)(
    void* container_handle,
    uint8_t channel_numbe,
    struct abn_llc_Channel* channel);

struct abn_llc_Llc_Functions {
  abn_llc_Llc_RegisterChannel add_channel;
  abn_llc_Llc_UnregisterChannel remove_channel;
  abn_llc_Llc_SignalTxDataAvailable signal_has_tx_data;
  abn_llc_Llc_SignalRxSpaceAvailable signal_rx_space_available;
  abn_llc_Llc_SignalCallbackNotification signal_callback_notification;
};

struct abn_llc_Llc_Interface {
  const struct abn_llc_Llc_Functions* functions;
  void* llc_handle;
};

ABN_EXTERN_C_END
