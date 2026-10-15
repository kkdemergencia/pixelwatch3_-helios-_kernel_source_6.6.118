/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google, Inc.
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

#include <asm/barrier.h>
#include <asm/rwonce.h>
#include <linux/atomic.h>

static inline int abn_sync_Atomic_LoadAcquire(abn_sync_Atomic* atomic)
{
  return atomic_read_acquire(atomic);
}

static inline int abn_sync_Atomic_ExchangeAcquire(abn_sync_Atomic* atomic,
                                                  int desired)
{
  return atomic_xchg_acquire(atomic, desired);
}

static inline void abn_sync_Atomic_StoreRelease(abn_sync_Atomic* atomic,
                                                int value)
{
  atomic_set_release(atomic, value);
}

static inline void abn_sync_Atomic_StoreRelaxed(abn_sync_Atomic* atomic,
                                                int value)
{
  atomic_set(atomic, value);
}
