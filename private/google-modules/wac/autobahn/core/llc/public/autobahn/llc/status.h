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

#include "autobahn/assert/assert.h"
#include "autobahn/autobahn.h"
#include "autobahn/llc/frame.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/string.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

// the implementation assume 8 is divisible by this value
#define ABN_LLC_NUM_BITS_PER_CHANNEL_STATUS 2
#define ABN_LLC_NUM_CHANNELS_PER_BYTE (8 / ABN_LLC_NUM_BITS_PER_CHANNEL_STATUS)
#define ABN_LLC_STATUS_SIZE_BYTES \
  (ABN_LLC_NUM_BITS_PER_CHANNEL_STATUS * ABN_LLC_NUM_CHANNELS / 8)
#define ABN_LLC_CHANNEL_STATUS_MASK 0x03

// This bit tells when a channel is available
// It is set in the following cases:
// - open channel (ABN_LLC_CHANNEL_AVAILABLE_BIT = 1)
// - close channel (ABN_LLC_CHANNEL_AVAILABLE_BIT = 0)
#define ABN_LLC_CHANNEL_AVAILABLE_BIT 1
#define ABN_LLC_CHANNEL_AVAILABLE_BIT_MASK \
  (0x01 << ABN_LLC_CHANNEL_AVAILABLE_BIT)

// This bit tells when a channel can receive data
// It is set in the following cases:
// - RX buffer full (ABN_LLC_CHANNEL_RX_FULL_BIT = 1)
// - RX buffer space available (ABN_LLC_CHANNEL_RX_FULL_BIT = 0)
#define ABN_LLC_CHANNEL_RX_FULL_BIT 0
#define ABN_LLC_CHANNEL_RX_FULL_BIT_MASK (0x01 << ABN_LLC_CHANNEL_RX_FULL_BIT)

struct abn_llc_Status {
  uint8_t status[ABN_LLC_STATUS_SIZE_BYTES];
};

ABN_STATIC_ASSERT(
    ABN_MAX_DATAGRAM_SIZE >= sizeof(struct abn_llc_Status),
    "sizeof(struct abn_llc_Status) is larger than ABN_MAX_DATAGRAM_SIZE");

inline static void abn_llc_Status_Init(struct abn_llc_Status* status) {
  memset(status->status, 0, ABN_LLC_STATUS_SIZE_BYTES);
}

// Set a particular bit inside channels buffer status
void abn_llc_Status_SetChannelBit(struct abn_llc_Status* status,
                                  uint8_t channel_index,
                                  uint8_t status_bit,
                                  bool value);

// Get a particular bit inside channels buffer status
bool abn_llc_Status_GetChannelBit(struct abn_llc_Status* status,
                                  uint8_t channel_index,
                                  uint8_t status_bit);

ABN_EXTERN_C_END
