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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "abn_msg_queue.h"
#include "autobahn/assert/assert.h"
#include "linux/list.h"

struct async_msg_priv {
	struct list_head list_head;
	bool release_waiters;
	uint8_t blocked_waiters;
};

bool abn_msg_queue_is_empty(struct abn_msg_queue *pabn_queue)
{
	bool is_empty;
	struct async_msg_priv *ppriv = pabn_queue->ppriv;

	spin_lock(&pabn_queue->wait_queue.lock);
	is_empty = list_empty(&ppriv->list_head);
	spin_unlock(&pabn_queue->wait_queue.lock);

	return is_empty;
}

void abn_msg_queue_push(struct abn_msg_queue *pabn_queue,
			struct abn_msg_elem *pabn_msg_elem)
{
	bool was_empty;
	struct async_msg_priv *ppriv = pabn_queue->ppriv;

	spin_lock(&pabn_queue->wait_queue.lock);
	was_empty = list_empty(&ppriv->list_head);
	list_add_tail(&pabn_msg_elem->list_head, &ppriv->list_head);
	spin_unlock(&pabn_queue->wait_queue.lock);

	if (was_empty)
		wake_up_interruptible(&pabn_queue->wait_queue);
}

static bool abn_msg_queue_wait_condition(struct abn_msg_queue *pabn_queue)
{
	bool ret;
	struct async_msg_priv *ppriv = pabn_queue->ppriv;

	spin_lock(&pabn_queue->wait_queue.lock);
	ret = !list_empty(&ppriv->list_head) ||
		    ppriv->release_waiters;
	spin_unlock(&pabn_queue->wait_queue.lock);

	return ret;
}

/* A mutex must be held between a peek() and a pop() on the same context,
 * in order to prevent from consuming data from a different context
 * after peek() and let it in an unknown state.
 */
struct abn_msg_elem *abn_msg_queue_peek(struct abn_msg_queue *pabn_queue)
{
	struct abn_msg_elem *pabn_msg_elem;
	struct async_msg_priv *ppriv = pabn_queue->ppriv;

	spin_lock(&pabn_queue->wait_queue.lock);

	pabn_msg_elem = list_first_entry_or_null(&ppriv->list_head,
				   struct abn_msg_elem, list_head);

	spin_unlock(&pabn_queue->wait_queue.lock);

	return (pabn_msg_elem != NULL) ? pabn_msg_elem : ERR_PTR(-ENOENT);
}

struct abn_msg_elem *abn_msg_queue_pop(struct abn_msg_queue *pabn_queue,
				       long timeout)
{
	struct abn_msg_elem *pabn_msg_elem = NULL;
	struct async_msg_priv *ppriv = pabn_queue->ppriv;

	spin_lock(&pabn_queue->wait_queue.lock);

	if (timeout > 0) {
		long ret;


		do {

			ppriv->blocked_waiters++;

			/* There's no locked version of
			 * wait_event_interruptible_timeout. Release the lock.
			 */
			spin_unlock(&pabn_queue->wait_queue.lock);
			ret = wait_event_interruptible_timeout(
				pabn_queue->wait_queue,
				abn_msg_queue_wait_condition(pabn_queue),
				timeout);
			spin_lock(&pabn_queue->wait_queue.lock);

			ppriv->blocked_waiters--;

			if (ret < 0) {
				spin_unlock(&pabn_queue->wait_queue.lock);
				return ERR_PTR(ret);
			} else if (ret == 0) {
				spin_unlock(&pabn_queue->wait_queue.lock);
				return ERR_PTR(-ETIME);
			}


			if (!list_empty(&ppriv->list_head) ||
			    ppriv->release_waiters)
				break;

			/* Since we unlocked the spinlock after wait_event
			 * verified the condition, another thread may have
			 * stolen our message. If that's the case and the queue
			 * is still empty, adjust the elapsed time and continue
			 * waiting.
			 */
			timeout -= ret;
		} while (timeout > 0);
	}

	if (!list_empty(&ppriv->list_head)) {
		pabn_msg_elem = list_first_entry(&ppriv->list_head,
				   struct abn_msg_elem, list_head);
		list_del(&pabn_msg_elem->list_head);
	}
	spin_unlock(&pabn_queue->wait_queue.lock);

	return (pabn_msg_elem != NULL) ? pabn_msg_elem : ERR_PTR(-ENOENT);
}

int abn_msg_queue_create(struct abn_msg_queue **ppabn_queue)
{
	struct abn_msg_queue *pabn_queue;
	struct async_msg_priv *ppriv;

	if (ppabn_queue == NULL)
		return -EINVAL;

	pabn_queue = kmalloc(sizeof(*pabn_queue), GFP_KERNEL);
	if (pabn_queue == NULL)
		return -ENOMEM;

	ppriv = kmalloc(sizeof(*ppriv), GFP_KERNEL);
	if (ppriv == NULL) {
		kfree(pabn_queue);
		return -ENOMEM;
	}
	pabn_queue->ppriv = ppriv;

	ppriv->blocked_waiters = 0;
	ppriv->release_waiters = false;
	INIT_LIST_HEAD(&ppriv->list_head);
	init_waitqueue_head(&pabn_queue->wait_queue);

	*ppabn_queue = pabn_queue;
	return 0;
}

int abn_msg_queue_destroy(struct abn_msg_queue *pabn_queue)
{
	struct async_msg_priv *ppriv = pabn_queue->ppriv;

	spin_lock(&pabn_queue->wait_queue.lock);
	ppriv->release_waiters = true;
	if (!list_empty(&ppriv->list_head) ||
		(ppriv->blocked_waiters != 0)) {
		spin_unlock(&pabn_queue->wait_queue.lock);
		wake_up_interruptible(&pabn_queue->wait_queue);
		return -EAGAIN;
	}
	spin_unlock(&pabn_queue->wait_queue.lock);

	kfree(ppriv);
	kfree(pabn_queue);
	return 0;
}
