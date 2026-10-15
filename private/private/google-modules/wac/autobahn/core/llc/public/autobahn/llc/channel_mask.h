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

#include "autobahn/autobahn.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/string.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

ABN_STATIC_ASSERT(ABN_LLC_NUM_CHANNELS % 8 == 0,
                  "Number of Autobahn LLC channels has to be divisible by 8");
#define ABN_LLC_CHANNELS_MASK_SIZE_BYTES (ABN_LLC_NUM_CHANNELS / 8)

struct abn_llc_ChannelMask {
  uint8_t mask[ABN_LLC_CHANNELS_MASK_SIZE_BYTES];
};

inline static void abn_llc_ChannelMask_Init(struct abn_llc_ChannelMask* mask) {
  memset(mask->mask, 0, ABN_LLC_CHANNELS_MASK_SIZE_BYTES);
}

void abn_llc_ChannelMask_Set(struct abn_llc_ChannelMask* mask,
                             uint8_t channel_index,
                             bool value);

bool abn_llc_ChannelMask_Get(struct abn_llc_ChannelMask* mask,
                             uint8_t channel_index);

ABN_EXTERN_C_END
