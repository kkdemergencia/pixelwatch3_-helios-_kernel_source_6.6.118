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

enum abn_mac_Pm_IdleState {
  ABN_MAC_PM_STATE_UNKNOWN,
  ABN_MAC_PM_STATE_ENTER_IDLE,
  ABN_MAC_PM_STATE_EXIT_IDLE,
};

// PM callback
//
// This is implemented by LLC
typedef void (*abn_mac_Pm_IdleCallback)(void* context,
                                        enum abn_mac_Pm_IdleState idle_state);

// Returns true if MAC is in IDLE, false otherwise
// Conditions for IDLE state
// - state == IDLE
// - no pending signal event
// - no pending transport event
typedef bool (*abn_mac_Pm_IsIdle)(void* mac_handle);

// Set the IDLE callback
//
// This callback is going to be called when MAC goes to IDLE state
typedef void (*abn_mac_Pm_SetIdleCallback)(void* container_handle,
                                           abn_mac_Pm_IdleCallback callback,
                                           void* context);

struct abn_mac_Pm_Functions {
  abn_mac_Pm_IsIdle is_idle;
  abn_mac_Pm_SetIdleCallback set_idle_callback;
};

// Public PM interface for the MAC for use by the singular client (data
// handler).
//
// This must be provided by the MAC responder and initiator implementations.
struct abn_mac_Pm {
  const struct abn_mac_Pm_Functions* functions;
  void* mac_handle;
};
