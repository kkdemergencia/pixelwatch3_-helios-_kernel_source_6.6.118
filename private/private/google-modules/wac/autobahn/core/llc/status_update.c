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

#include "autobahn/llc/status_update.h"

bool abn_llc_StatusUpdate_Available(struct abn_llc_StatusUpdate* status) {
  for (int i = 0; i < ABN_LLC_STATUS_UPDATE_SIZE_BYTES; i++) {
    if (status->update_available[i] != 0) {
      return true;
    }
  }

  return false;
}

void abn_llc_StatusUpdate_SetChannelBit(struct abn_llc_StatusUpdate* status,
                                        uint8_t channel_index,
                                        bool value) {
  uint8_t index_byte, index_channel_in_byte, bit_mask;

  index_byte = channel_index / ABN_LLC_STATUS_UPDATE_NUM_CHANNELS_PER_BYTE;
  index_channel_in_byte =
      channel_index % ABN_LLC_STATUS_UPDATE_NUM_CHANNELS_PER_BYTE;
  bit_mask = 0x01 << index_channel_in_byte;

  if (value) {
    status->update_available[index_byte] |= bit_mask;
  } else {
    status->update_available[index_byte] &= ~bit_mask;
  }
}

bool abn_llc_StatusUpdate_GetChannelBit(struct abn_llc_StatusUpdate* status,
                                        uint8_t channel_index) {
  uint8_t index_byte, index_channel_in_byte, bit_mask;

  index_byte = channel_index / ABN_LLC_STATUS_UPDATE_NUM_CHANNELS_PER_BYTE;
  index_channel_in_byte =
      channel_index % ABN_LLC_STATUS_UPDATE_NUM_CHANNELS_PER_BYTE;
  bit_mask = 0x01 << index_channel_in_byte;

  return (bool)(status->update_available[index_byte] & bit_mask);
}
