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

ABN_EXTERN_C_START

enum abn_llc_Pm_IdleState {
  ABN_LLC_PM_STATE_UNKNOWN,
  ABN_LLC_PM_STATE_ENTER_IDLE,
  ABN_LLC_PM_STATE_EXIT_IDLE,
};

// PM callback
//
// This is implemented by the PM client
typedef void (*abn_llc_Pm_IdleCallback)(void* context,
                                        enum abn_llc_Pm_IdleState idle_state);

// Check if LLC is in IDLE state
//
// Preconditions:
// - all LLC clients should be disabled
// in other words they should not be able to call read/write anymore
// - the function is responsible for checking also the MAC layer state
// however, it doesn't guarantee that the MAC layer is not going
// to start a transaction after IsIdle() is called
typedef bool (*abn_llc_Pm_IsIdle)(void* container_handle);

// Set the IDLE callback
//
// This callback is going to be called when LLC is in IDLE state:
// - no data to sent
// - no pending callback notifications (connection, read, write)
// - no ongoing writes
// - no pending ACK frame to be sent
// - no pending INIT frame to send
// This includes also the MAC layer state:
// - no ongoing transactions (MAC is in IDLE state)
// callback can be NULL (nothing is done by LLC when it becomes IDLE)
// To avoid any race condition, IsIdle() has to be called after
// SetIdleCallback() is called
typedef void (*abn_llc_Pm_SetIdleCallback)(void* container_handle,
                                           abn_llc_Pm_IdleCallback callback,
                                           void* context);

struct abn_llc_Pm_Functions {
  abn_llc_Pm_IsIdle is_idle;
  abn_llc_Pm_SetIdleCallback set_idle_callback;
};

struct abn_llc_Pm_Interface {
  const struct abn_llc_Pm_Functions* functions;
  void* llc_handle;
};

ABN_EXTERN_C_END
