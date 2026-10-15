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

#include "autobahn/mac/client.h"

typedef void (*abn_mac_Mac_SignalTxDataAvailable)(void* mac_handle);

struct abn_mac_Mac_Functions {
  abn_mac_Mac_SignalTxDataAvailable signal_tx_data_available;
};

// Public interface for the MAC for use by the singular client (data handler).
//
// This must be provided by the MAC responder and initiator implementations.
struct abn_mac_Mac {
  const struct abn_mac_Mac_Functions* functions;
  void* mac_handle;
};
