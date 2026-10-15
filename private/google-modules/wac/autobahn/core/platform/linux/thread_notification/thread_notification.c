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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>

#include "autobahn/sync/thread_notification_backend/thread_notification_native.h"

#define ABN_TN_LATCH_CLEARED 0
#define ABN_TN_LATCH_SET     1

void abn_sync_ThreadNotification_AcquireImpl(
		abn_sync_ThreadNotification *thread_notif)
{
	wait_event_idle(thread_notif->wait_queue,
		(kthread_should_stop() ||
		 atomic_cmpxchg_acquire(&thread_notif->latch,
				ABN_TN_LATCH_SET,
				ABN_TN_LATCH_CLEARED) == ABN_TN_LATCH_SET));
}

bool abn_sync_ThreadNotification_TryAcquireImpl(
		abn_sync_ThreadNotification *thread_notif)
{
	if (atomic_cmpxchg_acquire(&thread_notif->latch, ABN_TN_LATCH_SET,
				ABN_TN_LATCH_CLEARED) == ABN_TN_LATCH_CLEARED)
		return false;

	return true;
}

void abn_sync_ThreadNotification_ReleaseImpl(
		abn_sync_ThreadNotification *thread_notif)
{
	atomic_set_release(&thread_notif->latch, ABN_TN_LATCH_SET);
	wake_up(&thread_notif->wait_queue);
}

int abn_sync_ThreadNotification_Create(
		abn_sync_ThreadNotification **ppthread_notif)
{
	abn_sync_ThreadNotification *pthread_notif;

	if (ppthread_notif == NULL)
		return -EINVAL;

	pthread_notif = kmalloc(sizeof(**ppthread_notif), GFP_KERNEL);
	if (pthread_notif == NULL)
		return -ENOMEM;

	atomic_set(&pthread_notif->latch, ABN_TN_LATCH_CLEARED);
	init_waitqueue_head(&pthread_notif->wait_queue);

	*ppthread_notif = pthread_notif;
	return 0;
}

void abn_sync_ThreadNotification_Destroy(
		abn_sync_ThreadNotification *pthread_notif)
{
	kfree(pthread_notif);
}

