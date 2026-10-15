// SPDX-License-Identifier: GPL-2.0
/*
 * MCU interface autobahn implementation for i2c arbitration module
 *
 * Copyright 2026 Google LLC.
 */

#include <linux/kernel.h>
#include <linux/errno.h>

#include "i2c_arbitration_log.h"
#include "i2c_arbitration_mcu_interface.h"
#include "mcu_transport_interface.h"

#define MCU_I2C_ARBITRATION_MESSAGE_VERSION 0x01

static struct mcu_i2c_arbitration_callbacks *arbitration_callbacks;

struct mcu_i2c_arbitration_message {
	uint8_t version;
	uint8_t command;
	uint32_t seq_num;
} __packed;
static_assert(sizeof(struct mcu_i2c_arbitration_message) == 6,
	      "Unexpected size of mcu_i2c_arbitration_message");

static inline void assign_message(enum mcu_i2c_arbitration_command_type cmd, uint32_t seq_num,
				  struct mcu_i2c_arbitration_message *msg)
{
	if (unlikely(msg == NULL))
		return;

	msg->version = MCU_I2C_ARBITRATION_MESSAGE_VERSION;
	msg->command = (uint8_t)cmd;
	msg->seq_num = seq_num;
}

int mcu_send_i2c_control(enum i2c_arbitration_event_type type, uint32_t seq_num)
{
	ssize_t bytes;
	struct mcu_i2c_arbitration_message message;

	switch (type) {
	case I2C_OWNER_AP:
		assign_message(MCU_I2C_ARBITRATION_COMMAND_AP_GET_CONTROL, seq_num, &message);
		break;
	case I2C_OWNER_MCU:
		assign_message(MCU_I2C_ARBITRATION_COMMAND_MCU_GET_CONTROL, seq_num, &message);
		break;
	default:
		LOGE("%s: Get an unexpected i2c arbitration event type = %d\n", __func__, type);
		break;
	}

	bytes = mcu_transport_send_msg(MCU_TRANSPORT_I2C_ARBITRATION_CHANNEL_ID,
				       (const char *)&message, sizeof(message));
	if (bytes != sizeof(message)) {
		LOGE("MCU Bytes sent expected = %zd, actual = %zd\n", sizeof(message), bytes);
		return -EIO;
	}
	return 0;
}

int mcu_send_healthy_check(uint32_t seq_num)
{
	ssize_t bytes;
	struct mcu_i2c_arbitration_message message;

	assign_message(MCU_I2C_ARBITRATION_COMMAND_AP_SEND_ACK, seq_num, &message);

	bytes = mcu_transport_send_msg(MCU_TRANSPORT_I2C_ARBITRATION_CHANNEL_ID,
				       (const char *)&message, sizeof(message));
	if (bytes != sizeof(message)) {
		LOGE("MCU Bytes sent expected = %zd, actual = %zd\n", sizeof(message), bytes);
		return -EIO;
	}
	return 0;
}

static void on_mcu_message(const char *buffer, size_t length)
{
	struct mcu_i2c_arbitration_message *msg;

	// Check if the message size is expected
	if (length < sizeof(struct mcu_i2c_arbitration_message)) {
		LOGE("Received unexpected message size: %zu", length);
		return;
	}
	msg = (struct mcu_i2c_arbitration_message *)buffer;

	if (msg->version < MCU_I2C_ARBITRATION_MESSAGE_VERSION) {
		LOGE("Received unexpected message version: %u, expected at least %u", msg->version,
		     MCU_I2C_ARBITRATION_MESSAGE_VERSION);
		return;
	}
	LOGI("Received command type = %u", msg->command);
	switch (msg->command) {
	case MCU_I2C_ARBITRATION_COMMAND_MCU_SEND_ACK:
		if (!arbitration_callbacks || !arbitration_callbacks->handle_ack_from_mcu) {
			LOGE("No callbacks registered for MCU ACK");
			break;
		}
		arbitration_callbacks->handle_ack_from_mcu(msg->seq_num);
		break;
	case MCU_I2C_ARBITRATION_COMMAND_MCU_GET_CONTROL:
		if (arbitration_callbacks && arbitration_callbacks->handle_mcu_try_get_control)
			arbitration_callbacks->handle_mcu_try_get_control();
		break;
	// Receive unhandled rpc commands from MCU
	default:
		LOGW("Received unhandled command type : %u", msg->command);
		break;
	}
}

int i2c_arbitration_register_mcu_listener(void)
{
	return mcu_transport_register_channel_listener(MCU_TRANSPORT_I2C_ARBITRATION_CHANNEL_ID,
						       on_mcu_message);
}

void i2c_arbitration_register_mcu_listener_callbacks(
				   struct mcu_i2c_arbitration_callbacks *callbacks)
{
	if (callbacks == NULL)
		LOGW("No callbacks registered for MCU listener");
	arbitration_callbacks = callbacks;
}

void i2c_arbitration_unregister_mcu_listener(void)
{
	mcu_transport_unregister_channel_listener(MCU_TRANSPORT_I2C_ARBITRATION_CHANNEL_ID);
}
