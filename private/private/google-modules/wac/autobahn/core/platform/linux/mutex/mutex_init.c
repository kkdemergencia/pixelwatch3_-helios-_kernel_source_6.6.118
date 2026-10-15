// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "autobahn/sync/mutex_backend/mutex_init.h"

int abn_sync_Mutex_Create(abn_sync_Mutex **mutex)
{
	if (mutex == NULL)
		return -EINVAL;

	*mutex = kzalloc(sizeof(**mutex), GFP_KERNEL);
	if (*mutex == NULL)
		return -ENOMEM;

	spin_lock_init(*mutex);

	return 0;
}

void abn_sync_Mutex_Destroy(abn_sync_Mutex *mutex)
{
	if (mutex == NULL)
		return;

	kfree(mutex);
}
