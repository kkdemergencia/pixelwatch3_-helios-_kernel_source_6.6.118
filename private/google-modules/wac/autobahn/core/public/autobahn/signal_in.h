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
#include "autobahn/types.h"

ABN_EXTERN_C_START

typedef enum abn_Signal_State (*abn_SignalIn_Get)(void* signal_in_handle);

// The transition handling function that at least one level transition occurred.
//
// This may be invoked in an transition context.
typedef void (*abn_SignalIn_TransitionHandler)(void* handler_context);

// This cannot be used in transition handlers.
//
// The transition handler may be cleared by passing a nullptr.
//
// Preconditions:
// - The transition handler must be disabled.
typedef void (*abn_SignalIn_SetTransitionHandler)(
    void* signal_in_handle,
    abn_SignalIn_TransitionHandler handler,
    void* handler_context);

// This cannot be used in transition handlers.
//
// Preconditions:
// - The transition handler must be set to a valid handler before it is enabled.
typedef void (*abn_SignalIn_EnableTransitionHandler)(void* signal_in_handle,
                                                     bool enable);

struct abn_SignalIn_Functions {
  abn_SignalIn_Get get;
  abn_SignalIn_SetTransitionHandler set_transition_handler;
  abn_SignalIn_EnableTransitionHandler enable_transition_handler;
};

// abn_SignalIn instances are expected to be fully initialized, enabled with
// pull resisters enabled if needed, and with the transition handler disabled
// before Autobahn is started, this is the responsibility of the instantiator.
struct abn_SignalIn {
  const struct abn_SignalIn_Functions* functions;
  void* signal_in_handle;
};

ABN_EXTERN_C_END
