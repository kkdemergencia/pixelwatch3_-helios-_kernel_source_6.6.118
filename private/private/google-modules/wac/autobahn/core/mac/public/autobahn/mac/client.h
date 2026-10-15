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

#include "autobahn/config.h"
#include "autobahn/preprocessor/util.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

struct abn_mac_Frame {
  uint8_t buffer[ABN_FRAME_SIZE];
};

typedef bool (*abn_mac_Client_IsTxDataAvailable)(void* client_handle);
typedef void (*abn_mac_Client_PrepareBuffers)(
    void* client_handle,
    struct abn_mac_Frame** tx_frame_out,
    struct abn_mac_Frame** rx_frame_out);
// TODO: Discuss if we need to support variable size for transactions
// or a fixed size coming from a global variable/comstant
typedef void (*abn_mac_Client_ProcessRxData)(void* client_handle,
                                             struct abn_mac_Frame* buffer);
// TODO: Add an API for ReleaseBuffers() which will be called after the
// transaction is done or after a transaction is canceled

typedef void (*abn_mac_Client_HandleTransactionFailure)(void* client_handle);

struct abn_mac_Client_Functions {
  abn_mac_Client_IsTxDataAvailable is_tx_data_available;
  abn_mac_Client_PrepareBuffers prepare_buffers;
  abn_mac_Client_ProcessRxData process_rx_data;
  abn_mac_Client_HandleTransactionFailure handle_transaction_failure;
};

// Interface the of the singular user of the MAC.
struct abn_mac_Client {
  const struct abn_mac_Client_Functions* functions;
  void* client_handle;
};

ABN_EXTERN_C_END
