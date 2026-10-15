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

#include <kunit/test.h>

#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "autobahn/sync/thread_notification.h"
#include "autobahn/platform/linux/thread_notification.h"

MODULE_AUTHOR("Florin Curelaru");
MODULE_LICENSE("GPL");

#define THREAD_SLEEPING 1
#define THREAD_RUNNING  2
#define THREAD_EXITED   3

static abn_sync_ThreadNotification *pthread_notif;
static struct task_struct *ptask_struct;
static volatile int thread_stage;
static volatile unsigned int release_count;

static int thread_func(void *unused)
{
  while (1) {
    thread_stage = THREAD_SLEEPING;
    abn_sync_ThreadNotification_Acquire(pthread_notif);
    if (kthread_should_stop()) {
      thread_stage = THREAD_EXITED;
      return 0;
    }
    release_count++;
    thread_stage = THREAD_RUNNING;
    msleep(500);
  }
  do_exit(0);
  return 0;
}

static int abn_thread_notif_interf_init(struct kunit *test)
{
  pthread_notif = NULL;
  ptask_struct = NULL;
  thread_stage = 0;
  release_count = 0;
  return 0;
}

/* Test cases. */

static void abn_thread_notif_interf_test_creation(struct kunit *test)
{
  abn_sync_ThreadNotification_Create(&pthread_notif);
  abn_sync_ThreadNotification_Destroy(pthread_notif);

  KUNIT_EXPECT_TRUE(test, pthread_notif != NULL);
}

static void abn_thread_notif_interf_test_stop(struct kunit *test)
{
  abn_sync_ThreadNotification_Create(&pthread_notif);
  KUNIT_EXPECT_TRUE(test, pthread_notif != NULL);

  ptask_struct = kthread_create(thread_func, NULL, "TestThread");
  KUNIT_EXPECT_TRUE(test, ptask_struct != NULL);
  wake_up_process(ptask_struct);

  /* Wait for the thread to start */
  msleep(300);
  KUNIT_EXPECT_TRUE(test, thread_stage == THREAD_SLEEPING);

  /* Check that we can wake up the thread from sleeping
   * by calling kthread_stop(). */
  kthread_stop(ptask_struct);
  KUNIT_EXPECT_TRUE(test, thread_stage == THREAD_EXITED);

  abn_sync_ThreadNotification_Destroy(pthread_notif);
}

static void abn_thread_notif_interf_test_release(struct kunit *test)
{
  int local_thread_stage;

  abn_sync_ThreadNotification_Create(&pthread_notif);
  KUNIT_EXPECT_TRUE(test, pthread_notif != NULL);

  ptask_struct = kthread_create(thread_func, NULL, "TestThread");
  KUNIT_EXPECT_TRUE(test, ptask_struct != NULL);
  wake_up_process(ptask_struct);

  /* Wait for the thread to start */
  msleep(300);
  KUNIT_EXPECT_TRUE(test, thread_stage == THREAD_SLEEPING);

  /* Call release twice and check the operation happened once. */
  abn_sync_ThreadNotification_Release(pthread_notif);
  abn_sync_ThreadNotification_Release(pthread_notif);
  msleep(300);
  KUNIT_EXPECT_TRUE(test, release_count == 1);
  local_thread_stage = thread_stage;
  KUNIT_EXPECT_TRUE(test, (local_thread_stage == THREAD_SLEEPING) ||
                          (local_thread_stage == THREAD_RUNNING));

  /* Call release again and check the for the expected behaviour. */
  abn_sync_ThreadNotification_Release(pthread_notif);
  abn_sync_ThreadNotification_Release(pthread_notif);
  msleep(300);
  KUNIT_EXPECT_TRUE(test, release_count == 2);
  local_thread_stage = thread_stage;
  KUNIT_EXPECT_TRUE(test, (local_thread_stage == THREAD_SLEEPING) ||
                          (local_thread_stage == THREAD_RUNNING));

  /* Check that we can wake up the thread from sleeping
   * by calling kthread_stop(). */
  kthread_stop(ptask_struct);
  KUNIT_EXPECT_TRUE(test, thread_stage == THREAD_EXITED);

  abn_sync_ThreadNotification_Destroy(pthread_notif);
}

static void abn_thread_notif_interf_test_try_lock(struct kunit *test)
{
  bool try_lock_result;

  abn_sync_ThreadNotification_Create(&pthread_notif);

  /* Call release once and check for the right behavior three times in a row. */
  abn_sync_ThreadNotification_Release(pthread_notif);

  try_lock_result = abn_sync_ThreadNotification_TryAcquire(pthread_notif);
  KUNIT_EXPECT_EQ(test, true, try_lock_result);

  try_lock_result = abn_sync_ThreadNotification_TryAcquire(pthread_notif);
  KUNIT_EXPECT_EQ(test, false, try_lock_result);

  try_lock_result = abn_sync_ThreadNotification_TryAcquire(pthread_notif);
  KUNIT_EXPECT_EQ(test, false, try_lock_result);

  /* Call release again and check for the right behavior three times in a row. */
  abn_sync_ThreadNotification_Release(pthread_notif);

  try_lock_result = abn_sync_ThreadNotification_TryAcquire(pthread_notif);
  KUNIT_EXPECT_EQ(test, true, try_lock_result);

  try_lock_result = abn_sync_ThreadNotification_TryAcquire(pthread_notif);
  KUNIT_EXPECT_EQ(test, false, try_lock_result);

  try_lock_result = abn_sync_ThreadNotification_TryAcquire(pthread_notif);
  KUNIT_EXPECT_EQ(test, false, try_lock_result);

  /* Clean up the allocated resources. */
  abn_sync_ThreadNotification_Destroy(pthread_notif);
}

static struct kunit_case abn_thread_notif_interf_test_cases[] = {
  KUNIT_CASE(abn_thread_notif_interf_test_creation),
  KUNIT_CASE(abn_thread_notif_interf_test_stop),
  KUNIT_CASE(abn_thread_notif_interf_test_release),
  KUNIT_CASE(abn_thread_notif_interf_test_try_lock),
  {}
};

static struct kunit_suite abn_thread_notif_interf_test_suite = {
  .name = "abn_thread_notif_interf_test_suite",
  .init = abn_thread_notif_interf_init,
  .test_cases = abn_thread_notif_interf_test_cases
};

kunit_test_suite(abn_thread_notif_interf_test_suite);

