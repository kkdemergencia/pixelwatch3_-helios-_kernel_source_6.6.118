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
#include "autobahn/types.h"

ABN_EXTERN_C_START

// The mapping of timer ticks to real units is not known by Autobahn. The
// instantiator is expected to transparently pass through opaque durations in
// timer tick counts through the configuration of the various components of
// Autobahn.
typedef int64_t abn_Timer_Ticks;

// The timer expiry handler should be invoked once after abn_Timer_InvokeAfter
// is executed, unless aborted. After abn_Timer_Cancel returns, this callback
// shall no longer be invoked post expiry.
//
// The timer expiry handler is not permitted to control the timer itself, i.e.
// it may not invoke abn_Timer_Cancel nor abn_Timer_InvokeAfter.
//
// This may be invoked in an interrupt context.
typedef void (*abn_Timer_ExpiryHandler)(void* timer_handle);

// This is only executed as part of the initialization of Autobahn.
//
// Preconditions:
// - The expiry_handler must be a valid pointer.
//
// This function is not interrupt safe.
typedef void (*abn_Timer_SetExpiryHandler)(
    void* timer_handle,
    abn_Timer_ExpiryHandler expiry_handler,
    void* handler_context);

// Invokes the expiry callback as soon as possible after at least the specified
// duration in timer ticks.
//
// Implementations are expected to ensure that partial ticks are taken into
// account and ergo a tick may have to be added to the requested delay.
//
// Preconditions:
// - The expiry handler must be set to a valid handler.
//
// This function is not interrupt safe and ergo may not be executed by the
// abn_Timer_ExpiryHandler.
typedef void (*abn_Timer_InvokeAfter)(void* timer_handle,
                                      abn_Timer_Ticks delay);

// Cancels the timer expiry callback if pending.
//
// This function should be idempotent, i.e. it should be safe to call even if
// we never invoked abn_Timer_InvokeAfter.
//
// If the expiry callback is already being executed while you cancel it, it will
// finish callback execution to completion. You are responsible for any
// synchronization which is needed for thread safety.
//
// This function is not interrupt safe and ergo may not be executed by the
// abn_Timer_ExpiryHandler.
typedef void (*abn_Timer_Cancel)(void* timer_handle);

struct abn_Timer_Functions {
  abn_Timer_SetExpiryHandler set_expiry_handler;
  abn_Timer_InvokeAfter invoke_after;
  abn_Timer_Cancel cancel;
};

// abn_Timer instances are expected to be fully initialized before Autobahn is
// started, this is the responsibility of the intiatiator.
//
// Preconditions:
// - A single timer service implementation should back a full instantiation of
//   Autobahn with many abn_Timer instantiations to ensure they all behave the
//   same and interpret ticks the same way.
struct abn_Timer {
  const struct abn_Timer_Functions* functions;
  void* timer_handle;
};

ABN_EXTERN_C_END
