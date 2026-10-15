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

#include "autobahn/sync/thread_notification_backend/thread_notification_native.h"

int abn_sync_ThreadNotification_Create(
    abn_sync_ThreadNotification **ppthread_notif);

void abn_sync_ThreadNotification_Destroy(
    abn_sync_ThreadNotification *pthread_notif);
