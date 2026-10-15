// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google, Inc.
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

#include "autobahn/queue.h"

#include "autobahn/assert/assert.h"
#include "autobahn/string.h"
#include "autobahn/types.h"

inline static size_t abn_Queue_Min(size_t a, size_t b) { return a < b ? a : b; }

// Precondition: The caller must ensure the amount <= capacity.
inline static void abn_Queue_IncrementWithWrap(abn_Queue* queue,
                                               size_t* offset,
                                               size_t amount) {
  size_t capacity = abn_Queue_Capacity(queue);
  *offset += amount;
  // Note branch is faster than mod (%) on common embedded architectures.
  if (*offset >= capacity) {
    *offset -= capacity;
  }
}

inline static void abn_Queue_ContiguousChunkSizes(
    abn_Queue* queue,
    size_t offset,
    size_t data_size_bytes,
    size_t* first_chunk_size_out,
    size_t* second_chunk_size_out) {
  size_t first_chunk_size = 0;
  size_t second_chunk_size = 0;
  // The data may be wrapped around the buffer, so the data may have two
  // contigious chunks. As an example, where x are valid bytes:
  // [ x ] [ ] [ x ] [ x ], where offset = 2, data_size_bytes = 3, capacity = 4
  // first_chunk_size = 4 - 2 = 2.
  first_chunk_size = abn_Queue_Min(queue->capacity - offset, data_size_bytes);
  // second_chunk_size = 4 - 3 = 1.
  if (first_chunk_size == data_size_bytes) {
    second_chunk_size = 0;
  } else {
    second_chunk_size = data_size_bytes - first_chunk_size;
  }
  ABN_DASSERT(first_chunk_size + second_chunk_size == data_size_bytes);

  *first_chunk_size_out = first_chunk_size;
  *second_chunk_size_out = second_chunk_size;
}

void abn_Queue_Init(abn_Queue* queue,
                    uint8_t* buffer,
                    size_t buffer_size_bytes) {
  ABN_ASSERT(queue != NULL);
  queue->buffer = buffer;
  queue->capacity = buffer_size_bytes;

  abn_Queue_Clear(queue);
}

void abn_Queue_Clear(abn_Queue* queue) {
  queue->head = 0;
  queue->tail = 0;
  queue->count = 0;
}

void abn_Queue_Pop(abn_Queue* queue, uint8_t* dest, size_t data_size_bytes) {
  size_t first_chunk_size = 0;
  size_t second_chunk_size = 0;
  const size_t count = abn_Queue_Size(queue);
  ABN_ASSERT(data_size_bytes <= count);
  ABN_DASSERT(queue->head < queue->capacity);  // Integrity check.

  // The requested data may be wrapped around the buffer, so the data is copied
  // out in two chunks.
  abn_Queue_ContiguousChunkSizes(queue,
                                 queue->head,
                                 data_size_bytes,
                                 &first_chunk_size,
                                 &second_chunk_size);

  memcpy(dest, &queue->buffer[queue->head], first_chunk_size);
  if (second_chunk_size != 0) {
    memcpy(&dest[first_chunk_size], queue->buffer, second_chunk_size);
  }

  abn_Queue_IncrementWithWrap(queue, &queue->head, data_size_bytes);
  queue->count -= data_size_bytes;
}

bool abn_Queue_Push(abn_Queue* queue,
                    const uint8_t* data,
                    size_t data_size_bytes) {
  size_t first_chunk_size = 0;
  size_t second_chunk_size = 0;
  const size_t count = abn_Queue_Size(queue);
  const size_t capacity = abn_Queue_Capacity(queue);
  ABN_DASSERT(queue->tail < capacity);  // Integrity check.

  if (data_size_bytes > capacity - count) {
    return false;
  }

  // The requested data may be wrapped around the buffer, so the data is copied
  // out in two chunks.
  abn_Queue_ContiguousChunkSizes(queue,
                                 queue->tail,
                                 data_size_bytes,
                                 &first_chunk_size,
                                 &second_chunk_size);

  memcpy(&queue->buffer[queue->tail], data, first_chunk_size);
  if (second_chunk_size != 0) {
    memcpy(queue->buffer, &data[first_chunk_size], second_chunk_size);
  }

  abn_Queue_IncrementWithWrap(queue, &queue->tail, data_size_bytes);
  queue->count += data_size_bytes;

  return true;
}
