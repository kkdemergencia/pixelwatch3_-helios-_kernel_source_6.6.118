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

// This header must provide abn_sync_Mutex which shall:
// - Offer exclusive, non-recursive ownership semantics where priority
//   inheritance is used to solve the classic priority-inversion problem.
// - Be thread safe, but is not expected to be interrupt safe.
// - The instances are expected to be fully initialized before Autobahn is
//   started, this is the responsibility of the instantiator.
#include "autobahn/sync/mutex_backend/mutex_native.h"

ABN_EXTERN_C_START

// Locks the mutex, blocking indefinitely. Failures are fatal.
//
// Preconditions:
//  - The lock isn't already held by this thread. Recursive locking is undefined
//    behavior.
static inline void abn_sync_Mutex_Lock(abn_sync_Mutex* mutex);

// Unlocks the mutex. Failures are fatal.
//
// Preconditions:
//  - The mutex is held by this thread.
static inline void abn_sync_Mutex_Unlock(abn_sync_Mutex* mutex);

ABN_EXTERN_C_END

// Backends may refer to their non-inlined implementations from their
// forcefully public inlined methods, unfortunately due to GNU89 support
// we support use optional inline implementations.
#include "autobahn/sync/mutex_backend/mutex_inline.h"
