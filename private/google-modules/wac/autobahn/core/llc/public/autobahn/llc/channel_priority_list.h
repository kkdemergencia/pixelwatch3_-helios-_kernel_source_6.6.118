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

#include "autobahn/llc/channel_mask.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

struct abn_llc_ChannelPriorityList {
  // the values from this container are valid only for the channels which are
  // open; this container is in sync with channels containers
  // TODO: b/323974107 - Move this to abn_llc_Channel
  // Guarded by LLC mutex
  uint8_t channels_priorities[ABN_LLC_NUM_CHANNELS];

  // contains the number of channel which are open and have data to sent
  // Guarded by LLC mutex
  size_t number_channels_in_priority_list;

  // the values from this container are indexes to channels items
  // which are open and have data to sent.
  // only the first number_channels_in_priority_list items are valid
  // the order is based on channels priorities in descendent order
  // This is used for finding the next channel with data
  // Guarded by LLC mutex
  uint8_t channel_numbers_in_priority_order[ABN_LLC_NUM_CHANNELS];

  // For the channels which are in priority list, the values are true, false
  // otherwise. We need this values to avoid adding the channel multiple times
  // in priority_list This is because we can't control
  // abn_llc_Llc_TxDataAvailable() might be called multiple times for the same
  // channel Guarded by LLC mutex
  struct abn_llc_ChannelMask channels_in_priority_list;
};

// Initialize container variables
// This function is not thread-safe
void abn_llc_ChannelPriorityList_Init(
    struct abn_llc_ChannelPriorityList* priority_list);

// Set the priority of one of the channel (the channel might be opened, but not
// part of priority list)
// This function is not thread-safe
void abn_llc_ChannelPriorityList_SetChannelPriority(
    struct abn_llc_ChannelPriorityList* priority_list,
    uint8_t channel_number,
    uint8_t priority);

// Add a channel in priority list
// This function is not thread-safe
// Precondition:
// - there should be enough space to add a new element in the list
// For multiple channels with the same priority, the new item will be added
// after the existing item(s) with the same priority
void abn_llc_ChannelPriorityList_AddToList(
    struct abn_llc_ChannelPriorityList* priority_list, uint8_t channel_number);

// Try to remove a channel from priority list
// If it does, it returns true, else returns false
// This function is idempotent and not thread-safe
bool abn_llc_ChannelPriorityList_RemoveFromList(
    struct abn_llc_ChannelPriorityList* priority_list, uint8_t channel_number);

// Get next channel index in priority list
// This function is not thread-safe
// cookie is an internal state variable used by ChannelPriorityList.
// - before first call, it should be initialized with 0
// - its value should not be changed from outside
// Precondition:
// - there should be at least one element in the list
uint8_t abn_llc_ChannelPriorityList_GetNextChannelIndex(
    struct abn_llc_ChannelPriorityList* priority_list, uint8_t* cookie);

ABN_EXTERN_C_END
