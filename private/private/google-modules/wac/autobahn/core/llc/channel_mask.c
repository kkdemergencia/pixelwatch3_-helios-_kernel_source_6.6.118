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

#include "autobahn/llc/channel_mask.h"

#include "autobahn/types.h"

void abn_llc_ChannelMask_Set(struct abn_llc_ChannelMask* mask,
                             uint8_t channel_index,
                             bool value) {
  uint8_t byte_offset, shift_left, bit_offset, byte_mask;

  byte_offset = channel_index >> 3;
  bit_offset = channel_index & 0b111;
  shift_left = (7 - bit_offset);
  byte_mask = 0x1 << shift_left;

  if (value) {
    mask->mask[byte_offset] |= byte_mask;
  } else {
    mask->mask[byte_offset] &= ~byte_mask;
  }
}

bool abn_llc_ChannelMask_Get(struct abn_llc_ChannelMask* mask,
                             uint8_t channel_index) {
  uint8_t byte_offset, shift_left, bit_offset, byte_mask;

  byte_offset = channel_index >> 3;
  bit_offset = channel_index & 0b111;
  shift_left = (7 - bit_offset);
  byte_mask = 0x1 << shift_left;

  return (bool)(mask->mask[byte_offset] & byte_mask);
}
