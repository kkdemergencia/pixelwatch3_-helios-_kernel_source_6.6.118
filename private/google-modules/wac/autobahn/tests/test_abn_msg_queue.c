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
#include <linux/slab.h>
#include <linux/types.h>

#include "abn_msg_queue.h"

#define ABN_MSG_QUEUE_NO_TIMEOUT 0
#define ABN_MSG_QUEUE_POP_TIMEOUT msecs_to_jiffies(2000)

static struct abn_msg_queue *pabn_queue;

static struct task_struct *ptask_struct;
static struct completion task_completion;
static bool task_has_exited;
static bool task_to_exit;
static struct abn_msg_elem *pabn_msg_elem;

static int thread_wait_event_func(void *unused)
{
	wait_event_interruptible(pabn_queue->wait_queue, READ_ONCE(task_to_exit));
	WRITE_ONCE(task_has_exited, true);
	complete(&task_completion);
	return 0;
}

static int thread_push_elem_func(void *unused)
{
	struct abn_msg_elem *plocal_abn_msg_elem;

	/* Give time to the "_pop" function to block. */
	msleep(300);

	plocal_abn_msg_elem = kmalloc(sizeof(*plocal_abn_msg_elem), GFP_KERNEL);
	if (plocal_abn_msg_elem != NULL) {
		WRITE_ONCE(pabn_msg_elem, plocal_abn_msg_elem);
		abn_msg_queue_push(pabn_queue, plocal_abn_msg_elem);
	}

	complete(&task_completion);
	return 0;
}

/* Test cases. */

static void abn_msg_queue_test_creation(struct kunit *test)
{
	pabn_queue = NULL;
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_create(&pabn_queue) == 0);
	KUNIT_EXPECT_TRUE(test, pabn_queue != NULL);
	KUNIT_EXPECT_TRUE(test, waitqueue_active(&pabn_queue->wait_queue) == 0);

	KUNIT_EXPECT_TRUE(test, abn_msg_queue_destroy(pabn_queue) == 0);
}

static void abn_msg_queue_test_push_and_unblocking_pop(struct kunit *test)
{
	bool tmp_res;
	struct abn_msg_elem *pabn_msg_elem1 =
		kmalloc(sizeof(*pabn_msg_elem1), GFP_KERNEL);
	struct abn_msg_elem *pabn_msg_elem2 =
		kmalloc(sizeof(*pabn_msg_elem2), GFP_KERNEL);
	KUNIT_EXPECT_TRUE(test, pabn_msg_elem1 != NULL);
	KUNIT_EXPECT_TRUE(test, pabn_msg_elem2 != NULL);

	pabn_queue = NULL;
	abn_msg_queue_create(&pabn_queue);
	KUNIT_EXPECT_TRUE(test, pabn_queue != NULL);
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_is_empty(pabn_queue));
	KUNIT_EXPECT_TRUE(test, PTR_ERR(abn_msg_queue_pop(pabn_queue, ABN_MSG_QUEUE_NO_TIMEOUT)) == -ENOENT);

	/* Check that pushing the first message in an empty queue wakes up a process
	 * that is blocked on the public wait queue member of the queue.
	 */
	init_completion(&task_completion);
	WRITE_ONCE(task_has_exited, false);
	WRITE_ONCE(task_to_exit, false);
	ptask_struct = kthread_create(thread_wait_event_func, NULL, "TestThread");
	wake_up_process(ptask_struct);
	/* Give the thread some time to start and block. */
	msleep(300);
	KUNIT_EXPECT_TRUE(test, waitqueue_active(&pabn_queue->wait_queue) == 1);
	/* Push the first message and check that the thread ends its execution. */
	WRITE_ONCE(task_to_exit, true);
	abn_msg_queue_push(pabn_queue, pabn_msg_elem1);
	/* Give the thread some time to end its execution. */
	msleep(300);
	/* Without (m)sleep we would need a full memory barrier. */
	tmp_res = READ_ONCE(task_has_exited);
	/* Make sure that we release the thread when this test is failing. */
	if (!tmp_res)
		wake_up_interruptible(&pabn_queue->wait_queue);

	wait_for_completion(&task_completion);
	KUNIT_EXPECT_TRUE(test, tmp_res);
	KUNIT_EXPECT_TRUE(test, !abn_msg_queue_is_empty(pabn_queue));

	/* Check that pushing a message in a non empty queue doesn't wake up a
	 * potential process blocked on the public wait queue member of the queue.
	 */
	reinit_completion(&task_completion);
	WRITE_ONCE(task_has_exited, false);
	WRITE_ONCE(task_to_exit, false);
	ptask_struct = kthread_create(thread_wait_event_func, NULL, "TestThread");
	wake_up_process(ptask_struct);
	/* Give the thread some time to start and then block. */
	msleep(300);
	KUNIT_EXPECT_TRUE(test, waitqueue_active(&pabn_queue->wait_queue) == 1);
	WRITE_ONCE(task_to_exit, true);
	abn_msg_queue_push(pabn_queue, pabn_msg_elem2);
	msleep(300);
	tmp_res = READ_ONCE(task_has_exited);
	/* Release the thread. */
	wake_up_interruptible(&pabn_queue->wait_queue);
	wait_for_completion(&task_completion);
	KUNIT_EXPECT_TRUE(test, !tmp_res);
	KUNIT_EXPECT_TRUE(test, !abn_msg_queue_is_empty(pabn_queue));

	KUNIT_EXPECT_TRUE(test,
		   abn_msg_queue_pop(pabn_queue, ABN_MSG_QUEUE_NO_TIMEOUT) == pabn_msg_elem1);

	/* It shouldn't be possible to destroy the queue as it is not empty. */
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_destroy(pabn_queue) != 0);

	KUNIT_EXPECT_TRUE(test,
		   abn_msg_queue_pop(pabn_queue, ABN_MSG_QUEUE_POP_TIMEOUT) == pabn_msg_elem2);
	KUNIT_EXPECT_TRUE(test,
		   PTR_ERR(abn_msg_queue_pop(pabn_queue, ABN_MSG_QUEUE_NO_TIMEOUT)) == -ENOENT);

	KUNIT_EXPECT_TRUE(test, abn_msg_queue_destroy(pabn_queue) == 0);
}

static void abn_msg_queue_test_blocking_pop(struct kunit *test)
{
	struct abn_msg_elem *ptmp_msg_elem;

	pabn_queue = NULL;
	abn_msg_queue_create(&pabn_queue);
	KUNIT_EXPECT_TRUE(test, pabn_queue != NULL);
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_is_empty(pabn_queue));

	/* Check that the "_pop" function blocks until the first element is added. */
	init_completion(&task_completion);
	WRITE_ONCE(pabn_msg_elem, NULL);
	ptask_struct = kthread_create(thread_push_elem_func, NULL, "TestThread");
	wake_up_process(ptask_struct);
	ptmp_msg_elem = abn_msg_queue_pop(pabn_queue, ABN_MSG_QUEUE_POP_TIMEOUT);
	wait_for_completion(&task_completion);
	kfree(READ_ONCE(pabn_msg_elem));
	KUNIT_EXPECT_TRUE(test, READ_ONCE(pabn_msg_elem) == ptmp_msg_elem);
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_is_empty(pabn_queue));

	KUNIT_EXPECT_TRUE(test, abn_msg_queue_destroy(pabn_queue) == 0);
}

static void abn_msg_queue_test_peek_null(struct kunit *test)
{
	pabn_queue = NULL;
	abn_msg_queue_create(&pabn_queue);
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_peek(pabn_queue) == NULL);

	KUNIT_EXPECT_TRUE(test, abn_msg_queue_destroy(pabn_queue) == 0);
}

static void abn_msg_queue_test_peek(struct kunit *test)
{
	struct abn_msg_elem *pabn_msg_elem =
		kmalloc(sizeof(*pabn_msg_elem), GFP_KERNEL);
	struct abn_msg_elem *pabn_msg_elem1;

	pabn_queue = NULL;
	abn_msg_queue_create(&pabn_queue);
	KUNIT_EXPECT_TRUE(test, pabn_queue != NULL);
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_is_empty(pabn_queue));

	abn_msg_queue_push(pabn_queue, pabn_msg_elem);
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_peek(pabn_queue) == pabn_msg_elem);

	pabn_msg_elem1 = abn_msg_queue_pop(pabn_queue, ABN_MSG_QUEUE_POP_TIMEOUT);
	KUNIT_EXPECT_TRUE(test, pabn_msg_elem == pabn_msg_elem1);
	KUNIT_EXPECT_TRUE(test, abn_msg_queue_peek(pabn_queue) == NULL);

	KUNIT_EXPECT_TRUE(test, abn_msg_queue_destroy(pabn_queue) == 0);
}

static struct kunit_case abn_msg_queue_test_cases[] = {
	KUNIT_CASE(abn_msg_queue_test_creation),
	KUNIT_CASE(abn_msg_queue_test_push_and_unblocking_pop),
	KUNIT_CASE(abn_msg_queue_test_blocking_pop),
	KUNIT_CASE(abn_msg_queue_test_peek_null),
	KUNIT_CASE(abn_msg_queue_test_peek),
	{}
};

static struct kunit_suite abn_msg_queue_test_suite = {
	.name = "abn_msg_queue_test_suite",
	.test_cases = abn_msg_queue_test_cases
};

kunit_test_suite(abn_msg_queue_test_suite);

MODULE_AUTHOR("Florin Curelaru");
MODULE_LICENSE("GPL");
