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


#include "autobahn/llc/status.h"

void abn_llc_Status_SetChannelBit(struct abn_llc_Status* status,
                                  uint8_t channel_index,
                                  uint8_t status_bit,
                                  bool value) {
  uint8_t index_byte, shift, index_channel_in_byte;
  uint8_t channel_status_bits;
  uint8_t bit_mask = (0x01 << status_bit);

  index_byte = channel_index / ABN_LLC_NUM_CHANNELS_PER_BYTE;
  index_channel_in_byte = channel_index % ABN_LLC_NUM_CHANNELS_PER_BYTE;
  shift = (ABN_LLC_NUM_CHANNELS_PER_BYTE - index_channel_in_byte - 1) *
          ABN_LLC_NUM_BITS_PER_CHANNEL_STATUS;

  channel_status_bits = status->status[index_byte] >> shift;
  if (value) {
    channel_status_bits |= bit_mask;
  } else {
    channel_status_bits &= ~bit_mask;
  }
  channel_status_bits <<= shift;

  status->status[index_byte] &= ~(ABN_LLC_CHANNEL_STATUS_MASK << shift);
  status->status[index_byte] |= channel_status_bits;
}

bool abn_llc_Status_GetChannelBit(struct abn_llc_Status* status,
                                  uint8_t channel_index,
                                  uint8_t status_bit) {
  uint8_t index_byte, shift, index_channel_in_byte;
  uint8_t channel_status_bits;
  uint8_t bit_mask = (0x01 << status_bit);

  index_byte = channel_index / ABN_LLC_NUM_CHANNELS_PER_BYTE;
  index_channel_in_byte = channel_index % ABN_LLC_NUM_CHANNELS_PER_BYTE;
  shift = (ABN_LLC_NUM_CHANNELS_PER_BYTE - index_channel_in_byte - 1) *
          ABN_LLC_NUM_BITS_PER_CHANNEL_STATUS;

  channel_status_bits = status->status[index_byte] >> shift;
  return (bool)(channel_status_bits & bit_mask);
}
