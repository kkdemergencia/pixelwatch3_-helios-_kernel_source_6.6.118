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

#include <linux/spinlock.h>

static inline void abn_sync_Mutex_Lock(abn_sync_Mutex *mutex)
{
	spin_lock(mutex);
}

static inline void abn_sync_Mutex_Unlock(abn_sync_Mutex *mutex)
{
	spin_unlock(mutex);
}
