/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCU interface for i2c arbitration module header file
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _I2C_ARBITRATION_MCU_INTERFACE_H_
#define _I2C_ARBITRATION_MCU_INTERFACE_H_

#include <linux/types.h>

#include "i2c_arbitration_module.h"

/* Commands type should be aligned with MCU*/
enum mcu_i2c_arbitration_command_type {
	MCU_I2C_ARBITRATION_COMMAND_AP_SEND_ACK = 0x00,
	MCU_I2C_ARBITRATION_COMMAND_MCU_SEND_ACK = 0x01,
	MCU_I2C_ARBITRATION_COMMAND_AP_GET_CONTROL = 0x02,
	MCU_I2C_ARBITRATION_COMMAND_MCU_GET_CONTROL = 0x03,
	MCU_I2C_ARBITRATION_COMMAND_TYPE_MAX,
};
/* Register callbacks to MCU */
struct mcu_i2c_arbitration_callbacks {
	/* Handle for MCU_I2C_ARBITRATION_COMMAND_MCU_SEND_ACK */
	void (*handle_ack_from_mcu)(uint32_t seq_num);
	/* Handle for MCU_I2C_ARBITRATION_COMMAND_MCU_GET_CONTROL */
	void (*handle_mcu_try_get_control)(void);
};

extern int i2c_arbitration_register_mcu_listener(void);
extern void
i2c_arbitration_register_mcu_listener_callbacks(struct mcu_i2c_arbitration_callbacks *callbacks);
extern void i2c_arbitration_unregister_mcu_listener(void);
extern int mcu_send_i2c_control(enum i2c_arbitration_event_type type, uint32_t seq_num);
extern int mcu_send_healthy_check(uint32_t seq_num);
#endif // _I2C_ARBITRATION_MCU_INTERFACE_H_
