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

#include "autobahn/llc/channel_priority_list.h"

#include "autobahn/assert/assert.h"
#include "autobahn/string.h"

void abn_llc_ChannelPriorityList_Init(
    struct abn_llc_ChannelPriorityList* priority_list) {
  ABN_ASSERT(priority_list != NULL);

  priority_list->number_channels_in_priority_list = 0;
  memset(priority_list->channel_numbers_in_priority_order,
         0,
         sizeof(priority_list->channel_numbers_in_priority_order));
  abn_llc_ChannelMask_Init(&priority_list->channels_in_priority_list);
}

void abn_llc_ChannelPriorityList_SetChannelPriority(
    struct abn_llc_ChannelPriorityList* priority_list,
    uint8_t channel_number,
    uint8_t priority) {
  priority_list->channels_priorities[channel_number] = priority;
}

void abn_llc_ChannelPriorityList_AddToList(
    struct abn_llc_ChannelPriorityList* priority_list, uint8_t channel_number) {
  size_t i;
  uint8_t current_channel_number;
  uint8_t priority = priority_list->channels_priorities[channel_number];

  ABN_DASSERT(priority_list->number_channels_in_priority_list <
              ABN_LLC_NUM_CHANNELS);

  if (abn_llc_ChannelMask_Get(&priority_list->channels_in_priority_list,
                              channel_number)) {
    return;  // Already in the priority list.
  }
  abn_llc_ChannelMask_Set(
      &priority_list->channels_in_priority_list, channel_number, true);

  // If there are multiple channels with the same priority, the new item
  // is going to be added at the end of those in the list
  // Here is an example why:
  // - there are two channels with the same highest priority
  // - one of them is writing data at the same speed ABN sends it
  // - if we would add the channel in the beginning, all time the data will be
  // send from the same channel while the second one will wait in the 2nd place
  // in teh priority list
  // - since we add the channel at the end, the second channel will end up in
  // the first position and its data is sent
  for (i = 0; i < priority_list->number_channels_in_priority_list; ++i) {
    current_channel_number =
        priority_list->channel_numbers_in_priority_order[i];
    if (priority > priority_list->channels_priorities[current_channel_number]) {
      memmove(priority_list->channel_numbers_in_priority_order + i + 1,
              priority_list->channel_numbers_in_priority_order + i,
              priority_list->number_channels_in_priority_list - i);
      priority_list->channel_numbers_in_priority_order[i] = channel_number;
      ++priority_list->number_channels_in_priority_list;
      return;
    }
  }

  // this has the lowest priority so far, add it to the end
  priority_list->channel_numbers_in_priority_order
      [priority_list->number_channels_in_priority_list] = channel_number;
  ++priority_list->number_channels_in_priority_list;
}

bool abn_llc_ChannelPriorityList_RemoveFromList(
    struct abn_llc_ChannelPriorityList* priority_list, uint8_t channel_number) {
  size_t i;

  if (priority_list->number_channels_in_priority_list == 0) {
    return false;
  }

  if (!abn_llc_ChannelMask_Get(&priority_list->channels_in_priority_list,
                               channel_number)) {
    return false;
  }
  abn_llc_ChannelMask_Set(
      &priority_list->channels_in_priority_list, channel_number, false);

  for (i = 0; i < priority_list->number_channels_in_priority_list; ++i) {
    if (priority_list->channel_numbers_in_priority_order[i] == channel_number) {
      memmove(priority_list->channel_numbers_in_priority_order + i,
              priority_list->channel_numbers_in_priority_order + i + 1,
              priority_list->number_channels_in_priority_list - i - 1);
      --priority_list->number_channels_in_priority_list;
      return true;
    }
  }

  // we should never be here
  ABN_ASSERT(false);
}

inline static uint8_t abn_llc_ChannelPriorityList_GetNextIndexInPriorityList(
    struct abn_llc_ChannelPriorityList* priority_list,
    uint8_t index_in_priority_list) {
  uint8_t max_priority_channel, index_last_channel, index_next_channel;

  ABN_ASSERT(priority_list->number_channels_in_priority_list > 0);

  // If the last index is out of range, we start from the beginning of the list
  if (index_in_priority_list >=
      priority_list->number_channels_in_priority_list) {
    return 0;
  }

  index_last_channel =
      priority_list->channel_numbers_in_priority_order[index_in_priority_list];
  max_priority_channel = priority_list->channel_numbers_in_priority_order[0];

  // If the head of the list priority is different from the current index
  // we start from the beginning (since that priority is higher)
  if (priority_list->channels_priorities[max_priority_channel] !=
      priority_list->channels_priorities[index_last_channel]) {
    return 0;
  }

  // If we are not at the end of the list and the next element has the same
  // priority, we return that one (round robin). If the next priority is
  // smaller, we go back to the beginning of the list
  if (index_in_priority_list <
      priority_list->number_channels_in_priority_list - 1) {
    index_next_channel =
        priority_list
            ->channel_numbers_in_priority_order[index_in_priority_list + 1];
    if (priority_list->channels_priorities[index_next_channel] ==
        priority_list->channels_priorities[index_last_channel]) {
      return (index_in_priority_list + 1);
    }
  }

  return 0;
}

uint8_t abn_llc_ChannelPriorityList_GetNextChannelIndex(
    struct abn_llc_ChannelPriorityList* priority_list,
    uint8_t* previous_index_in_priority_list) {
  uint8_t index_in_priority_list =
      abn_llc_ChannelPriorityList_GetNextIndexInPriorityList(
          priority_list, *previous_index_in_priority_list);
  *previous_index_in_priority_list = index_in_priority_list;
  return priority_list
      ->channel_numbers_in_priority_order[index_in_priority_list];
}
