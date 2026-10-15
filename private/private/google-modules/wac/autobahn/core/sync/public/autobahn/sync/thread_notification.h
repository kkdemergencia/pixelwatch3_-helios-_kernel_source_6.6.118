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

// This header must provide abn_sync_ThreadNotification which shall:
// - Permit a SINGLE thread to block and consume a latching, saturating
//   notification from multiple notifiers.
// - Be effectively a subset of a binary semaphore API, except that only a
//   single thread can be notified and block at a time.
// - Permit interrupts to use abn_sync_ThreadNotification_Release, if it is
// necessary
//   in the instantiation of Autobahn on your platform.
// - Be initialized to being empty (latch is not set).
// - The instances are expected to be fully initialized before Autobahn is
//   started, this is the responsibility of the instantiator.
#include "autobahn/sync/thread_notification_backend/thread_notification_native.h"

ABN_EXTERN_C_START

// Blocks indefinitely until the thread is notified, i.e. until the
// notification latch can be cleared because it was set.
//
// Clears the notification latch.
//
// IMPORTANT: This should only be used by a single consumer thread.
static inline void abn_sync_ThreadNotification_Acquire(
    abn_sync_ThreadNotification* thread_notification);

// Returns whether the thread has been notified, i.e. whether the notification
// latch was set and resets the latch regardless.
//
// Clears the notification latch.
//
// Returns true if the thread was notified, meaning the internal latch was reset
// successfully.
//
// IMPORTANT: This should only be used by a single consumer thread.
static inline bool abn_sync_ThreadNotification_TryAcquire(
    abn_sync_ThreadNotification* thread_notification);

// Notifies the thread in a saturating manner, setting the notification
// latch.
//
// Raising the notification multiple time without it being acquired by the
// consuming thread is equivalent to raising the notification once to the
// thread. The notification is latched in case the thread was not waiting at
// the time.
//
// This is interrupt and thread safe.
static inline void abn_sync_ThreadNotification_Release(
    abn_sync_ThreadNotification* thread_notification);

ABN_EXTERN_C_END

// Backends may refer to their non-inlined implementations from their
// forcefully public inlined methods, unfortunately due to GNU89 support
// we support use optional inline implementations.
#include "autobahn/sync/thread_notification_backend/thread_notification_inline.h"
