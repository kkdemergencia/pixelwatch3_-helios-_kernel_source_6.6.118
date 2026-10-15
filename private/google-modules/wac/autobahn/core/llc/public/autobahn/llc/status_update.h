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

#include "autobahn/llc/frame.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/string.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

#define ABN_LLC_STATUS_UPDATE_NUM_CHANNELS_PER_BYTE 8
#define ABN_LLC_STATUS_UPDATE_SIZE_BYTES \
  (ABN_LLC_NUM_CHANNELS / ABN_LLC_STATUS_UPDATE_NUM_CHANNELS_PER_BYTE)

struct abn_llc_StatusUpdate {
  uint8_t update_available[ABN_LLC_STATUS_UPDATE_SIZE_BYTES];
};

inline static void abn_llc_StatusUpdate_Init(
    struct abn_llc_StatusUpdate* status) {
  memset(status, 0, sizeof(*status));
}

bool abn_llc_StatusUpdate_Available(struct abn_llc_StatusUpdate* status);

void abn_llc_StatusUpdate_SetChannelBit(struct abn_llc_StatusUpdate* status,
                                        uint8_t channel_index,
                                        bool value);

bool abn_llc_StatusUpdate_GetChannelBit(struct abn_llc_StatusUpdate* status,
                                        uint8_t channel_index);

ABN_EXTERN_C_END
