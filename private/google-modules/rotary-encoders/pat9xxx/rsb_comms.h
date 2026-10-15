/* SPDX-License-Identifier: GPL-2.0 */
/*
 * comms interface
 *
 * Copyright 2025 Google LLC.
 *
 */

#ifndef _PAT9XXX_COMMS_H_
#define _PAT9XXX_COMMS_H_

#include <linux/types.h>
#include "mcu_transport_interface.h"

/* RSB message headers
 * The header definitions need to be in sync with
 * the corresponding ones on the MCU side
 */

#define RSB_COMMAND_VERSION   1

enum rsb_cmd_type {
	RSB_EVENT_ACK         = 0,
	RSB_EVENT_COORDS      = 1,
	RSB_ENABLE_OFFLOAD    = 2,
	RSB_DISABLE_OFFLOAD   = 3,
	RSB_DRIVER_OPENED     = 4,
	RSB_DRIVER_CLOSED     = 5,
	RSB_DEBOUNCE_TIMEOUT  = 6,
	RSB_OFFLOAD_HANDSHAKE = 7,
	RSB_DISPLAY_SUSPEND   = 8,
	RSB_DISPLAY_RESUME    = 9,
};

struct rsb_cmd_header {
	uint8_t cmd_ver;
	uint8_t cmd;
} __packed;

struct rsb_generic_cmd_msg {
	struct rsb_cmd_header header;
} __packed;

struct rsb_event_coords_msg {
	struct rsb_cmd_header header;
	uint32_t id;
	int16_t delta_x;
	int16_t delta_y;
} __packed;

struct rsb_event_ack {
	struct rsb_cmd_header header;
	uint32_t id;
} __packed;

struct rsb_offload_enable_msg {
	struct rsb_cmd_header header;
	int16_t crown_sensitivity;
	int16_t wake_sensitivity;
	int16_t motion_threshold;
} __packed;

struct rsb_debounce_cmd_msg {
	struct rsb_cmd_header header;
} __packed;

int rsb_comms_register_listener(channel_handler handler);

void rsb_comms_unregister_listener(void);

void rsb_comms_msg(int16_t msg);

void rsb_comms_enable_offload(int16_t crown_sensitivity,
					int16_t wake_sensitivity,
					int16_t motion_threshold);

void rsb_comms_event_ack(uint32_t id);

#endif  // _PAT9XXX_COMMS_H_
