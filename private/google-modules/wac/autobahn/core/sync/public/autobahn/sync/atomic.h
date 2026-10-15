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

// This header must provide Must provide abn_sync_Atomic which shall:
// - Offer lock-free atomic access to an underlying `int` type.
// - Match the C++ and Linux Kernel memory ordering requirements for acquire,
//   release, and relaxed.
#include "autobahn/sync/atomic_backend/atomic_native.h"

ABN_EXTERN_C_START

// Meant for a thread to consume an atomic produced (released) by an interrupt.
static inline int abn_sync_Atomic_LoadAcquire(abn_sync_Atomic* atomic);

// Meant for a thread to consume an atomic produced by an interrupt in a
// lossless manner. The value is atomically exchanged with the desired value,
// where the existing value is returned.
static inline int abn_sync_Atomic_ExchangeAcquire(abn_sync_Atomic* atomic,
                                                  int desired);

// Meant for an interrupt to produce an atomic signal to a thread before
// awakening the thread using an abn_sync_ThreadNotification.
static inline void abn_sync_Atomic_StoreRelease(abn_sync_Atomic* atomic,
                                                int value);

// Meant for a thread to manage its atomic while the interrupt is disabled.
static inline void abn_sync_Atomic_StoreRelaxed(abn_sync_Atomic* atomic,
                                                int value);

ABN_EXTERN_C_END

// Backends may refer to their non-inlined implementations from their
// forcefully public inlined methods, unfortunately due to GNU89 support
// we support use optional inline implementations.
#include "autobahn/sync/atomic_backend/atomic_inline.h"
