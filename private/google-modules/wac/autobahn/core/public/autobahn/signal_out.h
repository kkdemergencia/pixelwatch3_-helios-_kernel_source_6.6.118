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

#include "autobahn/preprocessor/util.h"
#include "autobahn/signal.h"

ABN_EXTERN_C_START

// This cannot be used in interrupt handlers.
typedef void (*abn_SignalOut_Set)(void* signal_out_handle,
                                  enum abn_Signal_State state);

struct abn_SignalOut_Functions {
  abn_SignalOut_Set set;
};

// abn_SignalOut instances are expected to be fully initialized and enabled,
// with the signal set to inactive before Autobahn is started, this is the
// responsibility of the instantiator.
struct abn_SignalOut {
  const struct abn_SignalOut_Functions* functions;
  void* signal_out_handle;
};

ABN_EXTERN_C_END
