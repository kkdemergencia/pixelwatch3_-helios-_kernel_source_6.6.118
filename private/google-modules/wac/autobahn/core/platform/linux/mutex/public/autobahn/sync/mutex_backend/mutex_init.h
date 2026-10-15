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

#include "autobahn/sync/mutex.h"

/* Create and initialize an abn_sync_Mutex object. This must be followed by a
 * call to the Destroy method in order to release allocated resources.
 *
 * Returns 0 on success, and sets mutex to point to the created object.
 */
int abn_sync_Mutex_Create(abn_sync_Mutex **mutex);

/* Destroy an abn_sync_Mutex object and release its allocated resources. */
void abn_sync_Mutex_Destroy(abn_sync_Mutex *mutex);
