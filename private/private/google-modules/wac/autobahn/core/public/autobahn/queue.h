/* SPDX-License-Identifier: GPL-2.0-only */
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

#pragma once

#include "autobahn/preprocessor/util.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

// The Autobahn Queue is a not-thread-safe, bounded, FIFO byte queue which is
// specialized for moving spans of bytes.
typedef struct {
  uint8_t* buffer;
  size_t capacity;

  size_t head;  // Non-inclusive offset for the front (consumer).
  size_t tail;  // Non-inclusive offset for the back (producer).
  size_t count;
} abn_Queue;

void abn_Queue_Init(abn_Queue* queue,
                    uint8_t* buffer,
                    size_t buffer_size_bytes);

// Returns the maximum number of bytes the queue can hold.
static inline size_t abn_Queue_Capacity(abn_Queue* queue) {
  return queue->capacity;
}

// Returns the number of bytes in the queue.
static inline size_t abn_Queue_Size(abn_Queue* queue) { return queue->count; }

// Removes all bytes from the queue. After this call, Size returns zero.
void abn_Queue_Clear(abn_Queue* queue);

// Copies the requested data out of the queue.
//
// Preconditions:
// - There must be sufficient bytes in the queue (data_size_bytes <= Size()).
//
// This should only be used by the consumer.
void abn_Queue_Pop(abn_Queue* queue, uint8_t* dest, size_t data_size_bytes);

// Copies the requested data into the queue.
//
// Preconditions:
// - There must be space for the data (data_size_bytes <= Capacity() - Size()).
//
// This should only be used by the producer.
//
// Returns true on success or false if there is insufficient space available
// in the queue.
bool abn_Queue_Push(abn_Queue* queue,
                    const uint8_t* data,
                    size_t data_size_bytes);

ABN_EXTERN_C_END

#ifdef __cplusplus

#include <array>

namespace abn {

// A byte queue C++ wrapper of the abn_Queue without storage ownership.
// Use InlineQueueWithStorage<Capacity> for a version of the InlineQueue that
// owns the storage.
class InlineQueue {
 public:
  // We cannot use std::span until C++20 is adopted. Thus, the ctor below is
  // disabled.
  // InlineQueue(std::span<uint8_t> storage)
  //     : InlineQueue(storage.data(), storage.size()) {}

  InlineQueue(uint8_t* storage, size_t storage_size) {
    abn_Queue_Init(&queue_, storage, storage_size);
  }

  size_t capacity() { return abn_Queue_Capacity(&queue_); }

  size_t size() { return abn_Queue_Size(&queue_); }

  void Clear() { return abn_Queue_Clear(&queue_); }

  void Pop(uint8_t* dest, size_t data_size_bytes) {
    return abn_Queue_Pop(&queue_, dest, data_size_bytes);
  }

  bool Push(const uint8_t* data, size_t data_size_bytes) {
    return abn_Queue_Push(&queue_, data, data_size_bytes);
  }

 private:
  abn_Queue queue_;
};

namespace {

// Helps initialize the storage array before passing it to the
// InlineQueue ctor. Otherwise, the array will not be initialized by the time
// data() method is called.
// this is only temporary until std::span can be adopted.
template <size_t kCapacity>
struct Storage {
  std::array<uint8_t, kCapacity> storage{};
};

}  // namespace

template <size_t kCapacity>
class InlineQueueWithStorage : private Storage<kCapacity>, public InlineQueue {
 public:
  InlineQueueWithStorage()
      : Storage<kCapacity>(),
        InlineQueue(Storage<kCapacity>::storage.data(),
                    Storage<kCapacity>::storage.size()) {}
};

}  // namespace abn

#endif  // defined(__cplusplus)
