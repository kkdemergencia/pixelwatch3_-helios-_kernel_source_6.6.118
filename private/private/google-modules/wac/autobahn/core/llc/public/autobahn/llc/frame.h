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

#include "autobahn/assert/assert.h"
#include "autobahn/autobahn.h"
#include "autobahn/config.h"
#include "autobahn/string.h"
#include "autobahn/types.h"

// Types values can be up to 15, since we represent them on 3 bits
enum abn_llc_FrameType {
  // Empty frame
  ABN_LLC_FRAME_TYPE_EMPTY = 0x01,
  // Data frame
  ABN_LLC_FRAME_TYPE_DATAGRAM = 0x02,
  // Init frame
  ABN_LLC_FRAME_TYPE_INIT = 0x03,
  // Status frame
  ABN_LLC_FRAME_TYPE_STATUS = 0x04
};

// Ack attribute mask
#define ABN_LLC_FRAME_HEADER_ATTRIBUTE_ACK_MASK 0x80
// Re-init attribute mask
#define ABN_LLC_FRAME_HEADER_ATTRIBUTE_INIT_RECEIVED_MASK 0x40
// Sequence ID supported
#define ABN_LLC_FRAME_HEADER_ATTRIBUTE_SEQUENCE_ID_SUPPORTED_MASK 0x20
// Type attribute mask
#define ABN_LLC_FRAME_HEADER_ATTRIBUTE_TYPE_MASK 0x07

struct __attribute__((__packed__)) abn_llc_FrameHeader {
  //[7] ack
  // ack = 1 means the previous frame was valid
  // ack = 0 means there was no previous frame or it was invalid
  //[2:0] type
  uint8_t attributes;
  uint8_t channel;
  // The actual payload size is (payload_size_minus_one + 1)
  // Using uint8_t, we can only send 1-256 bytes.
  // For EMPTY frames, payload size is always zero.
#if ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED
  uint16_t payload_size_minus_one;
#else
  uint8_t payload_size_minus_one;
  uint8_t reserved;
#endif  // ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED
};

// Last RX Sequence ID received
#define ABN_LLC_FRAME_FOOTER_ATTRIBUTE_EXPECTED_INCOMING_FLOW_SEQUENCE_ID_MASK \
  0x0C
#define ABN_LLC_FRAME_FOOTER_ATTRIBUTE_EXPECTED_INCOMING_FLOW_SEQUENCE_ID_SHIFT \
  2

// Current TX Sequence ID sent
#define ABN_LLC_FRAME_FOOTER_ATTRIBUTE_OUTGOING_FLOW_SEQUENCE_ID_MASK 0x03

struct __attribute__((__packed__)) abn_llc_FrameFooter {
  // [3:2] RX Sequence ID expected
  // [1:0] TX Sequence ID
  uint8_t attributes;
  uint8_t reserved;
  // The integrity check covers the entire frame up to but not including
  // the integrity_check itself.
  uint16_t integrity_check;
};

static_assert(ABN_FRAME_SIZE > sizeof(struct abn_llc_FrameHeader) +
                                   sizeof(struct abn_llc_FrameFooter),
              "Autobahn frame size too small");

#define ABN_MAX_DATAGRAM_SIZE                            \
  (ABN_FRAME_SIZE - sizeof(struct abn_llc_FrameHeader) - \
   sizeof(struct abn_llc_FrameFooter))

struct __attribute__((__packed__)) abn_llc_Frame {
  struct abn_llc_FrameHeader header;
  // Unused payload bytes shall be zero.
  uint8_t payload[ABN_MAX_DATAGRAM_SIZE];
  struct abn_llc_FrameFooter footer;
};

// This is the content of the INIT frame
// The position/size of the version field should not change in the future
// revisions
struct abn_llc_Llc_InitFrame_Data {
  // Autobahn LLC version protocol
  uint8_t version;
};

// TODO: Enable this precondition
// #if sizeof(struct abn_llc_Frame_Data) != ABN_FRAME_SIZE
// #error "Invalid Frame size"
// #endif

// Preconditions:
// - input_buffer must be a valid pointer
// - input_buffer_size is the size of input_buffer
// - result must be a valid pointer
// The result of processing the check is stored in result
typedef uint16_t (*abn_llc_Frame_IntegrityChecker)(uint8_t* input_buffer,
                                                   size_t input_buffer_size);

// This is how the frame should be used:
// TX frame
// For type == EMPTY
//  - call abn_llc_Frame_PrepareEmpty()
// For frame == INIT
//  - call abn_llc_Frame_PrepareInit()
// For type == DATAGRAM
//  - call abn_llc_Frame_GetRawDataBuffer() to get the buffer
//  - populate the buffer
//  - call abn_llc_Frame_PrepareData()
// For type == STATUS
//  - call abn_llc_Frame_GetRawDataBuffer() to get the buffer
//  - populate the buffer
//  - call abn_llc_Frame_PrepareStatus()
// RX frame
// Call abn_llc_Frame_IsValid() to validate the frame
// Based on frame->type, decide what to do
// For DATA and STATUS, call abn_llc_FrameGetActualRawDataBuffer()
// and interpret the actual data

// Return true when ack is set, meaning that the previous frame was valid
static inline bool abn_llc_Frame_GetAck(const struct abn_llc_Frame* frame) {
  return (frame->header.attributes & ABN_LLC_FRAME_HEADER_ATTRIBUTE_ACK_MASK) !=
         0;
}

static inline bool abn_llc_Frame_GetInitReceived(
    const struct abn_llc_Frame* frame) {
  return (frame->header.attributes &
          ABN_LLC_FRAME_HEADER_ATTRIBUTE_INIT_RECEIVED_MASK) != 0;
}

static inline enum abn_llc_FrameType abn_llc_Frame_GetType(
    const struct abn_llc_Frame* frame) {
  return (enum abn_llc_FrameType)(frame->header.attributes &
                                  ABN_LLC_FRAME_HEADER_ATTRIBUTE_TYPE_MASK);
}

static inline uint8_t abn_llc_Frame_GetExpectedSequenceId(
    const struct abn_llc_Frame* frame) {
  return (frame->footer.attributes &
          ABN_LLC_FRAME_FOOTER_ATTRIBUTE_EXPECTED_INCOMING_FLOW_SEQUENCE_ID_MASK) >>
         ABN_LLC_FRAME_FOOTER_ATTRIBUTE_EXPECTED_INCOMING_FLOW_SEQUENCE_ID_SHIFT;
}

static inline uint8_t abn_llc_Frame_GetOutgoingSequenceId(
    const struct abn_llc_Frame* frame) {
  return (frame->footer.attributes &
          ABN_LLC_FRAME_FOOTER_ATTRIBUTE_OUTGOING_FLOW_SEQUENCE_ID_MASK);
}

static inline uint8_t abn_llc_Frame_GetNextSequenceId(uint8_t sequence_id) {
  return ((sequence_id + 1) &
          ABN_LLC_FRAME_FOOTER_ATTRIBUTE_OUTGOING_FLOW_SEQUENCE_ID_MASK);
}

static inline uint8_t abn_llc_Frame_GetPrevSequenceID(uint8_t sequence_id) {
  return ((sequence_id - 1) &
          ABN_LLC_FRAME_FOOTER_ATTRIBUTE_OUTGOING_FLOW_SEQUENCE_ID_MASK);
}

static inline bool abn_llc_Frame_IsSequenceIdSupported(
    struct abn_llc_Frame* frame) {
  return frame->header.attributes &
         ABN_LLC_FRAME_HEADER_ATTRIBUTE_SEQUENCE_ID_SUPPORTED_MASK;
}

static inline uint16_t abn_llc_Frame_ComputeIntegrityCheck(
    struct abn_llc_Frame* frame, abn_llc_Frame_IntegrityChecker compute_check) {
  return compute_check(
      (uint8_t*)frame,
      sizeof(struct abn_llc_Frame) - sizeof(frame->footer.integrity_check));
}

// Check if the frame is not corrupted
static inline bool abn_llc_Frame_IsValid(
    struct abn_llc_Frame* frame, abn_llc_Frame_IntegrityChecker compute_check) {
  uint16_t computed_integrity_check =
      abn_llc_Frame_ComputeIntegrityCheck(frame, compute_check);
  return (computed_integrity_check == frame->footer.integrity_check);
}

static inline uint8_t abn_llc_Frame_HeaderAttributes(
    bool ack_previous_frame,
    bool reinit,
    enum abn_llc_FrameType type,
    bool sequence_id_supported) {
  uint8_t attributes = (uint8_t)type;
  if (ack_previous_frame) {
    attributes |= ABN_LLC_FRAME_HEADER_ATTRIBUTE_ACK_MASK;
  }

  if (reinit) {
    attributes |= ABN_LLC_FRAME_HEADER_ATTRIBUTE_INIT_RECEIVED_MASK;
  }

  if (sequence_id_supported) {
    attributes |= ABN_LLC_FRAME_HEADER_ATTRIBUTE_SEQUENCE_ID_SUPPORTED_MASK;
  }

  return attributes;
}

static inline uint8_t abn_llc_Frame_FooterAttributes(
    uint8_t expected_incoming_flow_sequence_id,
    uint8_t outgoing_flow_sequence_id) {
  uint8_t attributes =
      outgoing_flow_sequence_id &
      ABN_LLC_FRAME_FOOTER_ATTRIBUTE_OUTGOING_FLOW_SEQUENCE_ID_MASK;

  attributes |=
      ((expected_incoming_flow_sequence_id
        << ABN_LLC_FRAME_FOOTER_ATTRIBUTE_EXPECTED_INCOMING_FLOW_SEQUENCE_ID_SHIFT) &
       ABN_LLC_FRAME_FOOTER_ATTRIBUTE_EXPECTED_INCOMING_FLOW_SEQUENCE_ID_MASK);

  return attributes;
}

static inline void abn_llc_Frame_SetAckAndUpdateIntegrityCheck(
    struct abn_llc_Frame* frame,
    bool ack,
    abn_llc_Frame_IntegrityChecker compute_check) {
  if (ack) {
    frame->header.attributes |= ABN_LLC_FRAME_HEADER_ATTRIBUTE_ACK_MASK;
  } else {
    frame->header.attributes &= ~ABN_LLC_FRAME_HEADER_ATTRIBUTE_ACK_MASK;
  }

  frame->footer.integrity_check =
      abn_llc_Frame_ComputeIntegrityCheck(frame, compute_check);
}

static inline void abn_llc_Frame_SetAckRxSequenceIdAndUpdateIntegrityCheck(
    struct abn_llc_Frame* frame,
    uint8_t rx_sequence_id,
    bool ack,
    abn_llc_Frame_IntegrityChecker compute_check) {
  uint8_t tx_sequence_id;

  if (ack) {
    frame->header.attributes |= ABN_LLC_FRAME_HEADER_ATTRIBUTE_ACK_MASK;
  } else {
    frame->header.attributes &= ~ABN_LLC_FRAME_HEADER_ATTRIBUTE_ACK_MASK;
  }

  tx_sequence_id = abn_llc_Frame_GetOutgoingSequenceId(frame);
  frame->footer.attributes =
      abn_llc_Frame_FooterAttributes(rx_sequence_id, tx_sequence_id);

  frame->footer.integrity_check =
      abn_llc_Frame_ComputeIntegrityCheck(frame, compute_check);
}

static inline void abn_llc_Frame_SetInvalidPayloadBytes(
    struct abn_llc_Frame* frame, size_t start_invalid_payload_bytes) {
  ABN_ASSERT(start_invalid_payload_bytes <= ABN_MAX_DATAGRAM_SIZE);

  memset((uint8_t*)frame->payload + start_invalid_payload_bytes,
         0,
         ABN_MAX_DATAGRAM_SIZE - start_invalid_payload_bytes);
}

static inline void abn_llc_Frame_PrepareEmpty(
    struct abn_llc_Frame* frame,
    bool ack_previous_frame,
    uint8_t expected_incoming_flow_sequence_id,
    uint8_t outgoing_flow_sequence_id,
    abn_llc_Frame_IntegrityChecker compute_check) {
  // Header
  frame->header.attributes =
      abn_llc_Frame_HeaderAttributes(ack_previous_frame,
                                     /*reinit=*/false,
                                     ABN_LLC_FRAME_TYPE_EMPTY,
                                     /*sequence_id_supported*/ true);
  frame->header.channel = 0;
  frame->header.payload_size_minus_one = 0;
#if !ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED
  frame->header.reserved = 0;
#endif  // ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED

  // Payload
  abn_llc_Frame_SetInvalidPayloadBytes(frame, 0);

  // Footer
  frame->footer.attributes = abn_llc_Frame_FooterAttributes(
      expected_incoming_flow_sequence_id, outgoing_flow_sequence_id);
  frame->footer.reserved = 0;
  frame->footer.integrity_check =
      abn_llc_Frame_ComputeIntegrityCheck(frame, compute_check);
}

// Precondition:
//  - The INIT payload was already initialized by the upper layer before calling
//  this function
//  - the payload should contain a valid abn_llc_Llc_InitFrame_Data object
static inline void abn_llc_Frame_PrepareInit(
    struct abn_llc_Frame* frame,
    bool ack_previous_frame,
    bool is_reinit,
    uint8_t expected_incoming_flow_sequence_id,
    uint8_t outgoing_flow_sequence_id,
    bool sequence_id_supported,
    abn_llc_Frame_IntegrityChecker compute_check) {
  // Header
  size_t size = sizeof(struct abn_llc_Llc_InitFrame_Data);

  frame->header.attributes =
      abn_llc_Frame_HeaderAttributes(ack_previous_frame,
                                     is_reinit,
                                     ABN_LLC_FRAME_TYPE_INIT,
                                     sequence_id_supported);
  frame->header.channel = 0;
  frame->header.payload_size_minus_one = size - 1;
#if !ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED
  frame->header.reserved = 0;
#endif  // ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED

  // Payload
  abn_llc_Frame_SetInvalidPayloadBytes(frame, size);

  // Footer
  frame->footer.attributes = abn_llc_Frame_FooterAttributes(
      expected_incoming_flow_sequence_id, outgoing_flow_sequence_id);
  frame->footer.reserved = 0;
  frame->footer.integrity_check =
      abn_llc_Frame_ComputeIntegrityCheck(frame, compute_check);
}

// The valid payload must already be set in the frame.
//
// Precondition: The payload size must be > 0 & <= ABN_MAX_DATAGRAM_SIZE
static inline void abn_llc_Frame_PrepareData(
    struct abn_llc_Frame* frame,
    bool ack_previous_frame,
    uint8_t index_channel,
    size_t size,
    uint8_t expected_incoming_flow_sequence_id,
    uint8_t outgoing_flow_sequence_id,
    abn_llc_Frame_IntegrityChecker compute_check) {
  ABN_ASSERT(size > 0);
  ABN_ASSERT(size <= ABN_MAX_DATAGRAM_SIZE);

  // Header
  frame->header.attributes =
      abn_llc_Frame_HeaderAttributes(ack_previous_frame,
                                     /*reinit=*/false,
                                     ABN_LLC_FRAME_TYPE_DATAGRAM,
                                     /*sequence_id_supported*/ true);
  frame->header.channel = index_channel;
  frame->header.payload_size_minus_one = size - 1;
#if !ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED
  frame->header.reserved = 0;
#endif  // ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED

  // Payload
  abn_llc_Frame_SetInvalidPayloadBytes(frame, size);

  // Footer
  frame->footer.attributes = abn_llc_Frame_FooterAttributes(
      expected_incoming_flow_sequence_id, outgoing_flow_sequence_id);
  frame->footer.reserved = 0;
  frame->footer.integrity_check =
      abn_llc_Frame_ComputeIntegrityCheck(frame, compute_check);
}

// The valid payload must already be set in the frame.
//
// Precondition: The payload size must be > 0 & <= ABN_MAX_DATAGRAM_SIZE
static inline void abn_llc_Frame_PrepareStatus(
    struct abn_llc_Frame* frame,
    bool ack_previous_frame,
    size_t size,
    uint8_t expected_incoming_flow_sequence_id,
    uint8_t outgoing_flow_sequence_id,
    abn_llc_Frame_IntegrityChecker compute_check) {
  ABN_ASSERT(size > 0);
  ABN_ASSERT(size <= ABN_MAX_DATAGRAM_SIZE);

  // Header
  frame->header.attributes =
      abn_llc_Frame_HeaderAttributes(ack_previous_frame,
                                     /*reinit=*/false,
                                     ABN_LLC_FRAME_TYPE_STATUS,
                                     /*sequence_id_supported*/ true);
  frame->header.channel = 0;
  frame->header.payload_size_minus_one = size - 1;
#if !ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED
  frame->header.reserved = 0;
#endif  // ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED

  // Payload
  abn_llc_Frame_SetInvalidPayloadBytes(frame, size);

  // Footer
  frame->footer.attributes = abn_llc_Frame_FooterAttributes(
      expected_incoming_flow_sequence_id, outgoing_flow_sequence_id);
  frame->footer.reserved = 0;
  frame->footer.integrity_check =
      abn_llc_Frame_ComputeIntegrityCheck(frame, compute_check);
}

// Get access to the full payload buffer, note it has a size of
// ABN_MAX_DATAGRAM_SIZE.
//
// Preconditions:
// - The frame must not be used to produce an EMPTY type frame,
//   else your payload will be overwritten to zero.
static inline void abn_llc_Frame_GetPayloadBuffer(struct abn_llc_Frame* frame,
                                                  uint8_t** payload_out) {
  *payload_out = frame->payload;
}

// Get the actual payload buffer and size to process it
//
// Preconditions:
// - The frame must not be an EMPTY type frame.
static inline void abn_llc_Frame_GetPayload(struct abn_llc_Frame* frame,
                                            uint8_t** payload_out,
                                            size_t* payload_size_out) {
  ABN_ASSERT(abn_llc_Frame_GetType(frame) != ABN_LLC_FRAME_TYPE_EMPTY);
  *payload_out = frame->payload;
  *payload_size_out = frame->header.payload_size_minus_one + 1;
}
