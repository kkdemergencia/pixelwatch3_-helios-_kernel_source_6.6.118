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

#ifndef _ABN_MSG_QUEUE_H
#define _ABN_MSG_QUEUE_H

#include <linux/list.h>
#include <linux/wait.h>
#include <linux/types.h>

struct abn_msg_elem {
	struct list_head list_head;
	uint8_t buff[];
};

struct abn_msg_queue {
	/* Left the wait queue public to be used for implementing POLL. */
	wait_queue_head_t wait_queue;

	void *ppriv;
};

bool abn_msg_queue_is_empty(struct abn_msg_queue *pabn_queue);

void abn_msg_queue_push(struct abn_msg_queue *pabn_queue,
			struct abn_msg_elem *pabn_msg_elem);

struct abn_msg_elem *abn_msg_queue_peek(struct abn_msg_queue *pabn_queue);

struct abn_msg_elem *abn_msg_queue_pop(struct abn_msg_queue *pabn_queue,
				       long timeout);

int abn_msg_queue_create(struct abn_msg_queue **ppabn_queue);

int abn_msg_queue_destroy(struct abn_msg_queue *pabn_queue);

#endif /* _ABN_MSG_QUEUE_H */
