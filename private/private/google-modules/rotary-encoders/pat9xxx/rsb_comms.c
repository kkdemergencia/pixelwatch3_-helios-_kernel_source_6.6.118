// SPDX-License-Identifier: GPL-2.0
/*
 * comms transport interface
 *
 * Copyright 2025 Google LLC.
 *
 */

#include <linux/string.h>

#include "rsb_comms.h"

int rsb_comms_register_listener(channel_handler handler)
{
	return mcu_transport_register_channel_listener(MCU_TRANSPORT_RSB_CHANNEL_ID, handler);
}

void rsb_comms_unregister_listener(void)
{
	mcu_transport_unregister_channel_listener(MCU_TRANSPORT_RSB_CHANNEL_ID);
}

void rsb_comms_msg(int16_t msg)
{
	struct rsb_generic_cmd_msg cmd_msg;

	cmd_msg.header.cmd_ver = RSB_COMMAND_VERSION;
	cmd_msg.header.cmd = msg;
	mcu_transport_send_msg(MCU_TRANSPORT_RSB_CHANNEL_ID, (const char *) &cmd_msg,
				sizeof(cmd_msg));
}

void rsb_comms_enable_offload(int16_t crown_sensitivity,
					int16_t wake_sensitivity,
					int16_t motion_threshold)
{
	struct rsb_offload_enable_msg msg;

	msg.header.cmd_ver    = RSB_COMMAND_VERSION;
	msg.header.cmd        = RSB_ENABLE_OFFLOAD;
	memcpy(&msg.crown_sensitivity, &crown_sensitivity, sizeof(int16_t));
	memcpy(&msg.wake_sensitivity, &wake_sensitivity, sizeof(int16_t));
	memcpy(&msg.motion_threshold, &motion_threshold, sizeof(int16_t));
	mcu_transport_send_msg(MCU_TRANSPORT_RSB_CHANNEL_ID, (const char *) &msg,
				sizeof(msg));
}

void rsb_comms_event_ack(uint32_t id)
{
	struct rsb_event_ack ack;

	ack.header.cmd_ver = RSB_COMMAND_VERSION;
	ack.header.cmd = RSB_EVENT_ACK;
	memcpy(&ack.id, &id, sizeof(uint32_t));
	mcu_transport_send_msg(MCU_TRANSPORT_RSB_CHANNEL_ID, (const char *) &ack,
				sizeof(ack));
}
