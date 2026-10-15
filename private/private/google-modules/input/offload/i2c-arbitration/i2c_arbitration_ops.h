/* SPDX-License-Identifier: GPL-2.0 */
/*
 * I2C Arbitration Operations header
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _I2C_ARBITRATION_OPS_H_
#define _I2C_ARBITRATION_OPS_H_

#include "i2c_arbitration_module.h"

/**
 * struct i2c_arbitration_ops - Backend operations for I2C arbitration
 * @take_control_back_ap: Request to take control back to AP
 * @yield_control_to_mcu: Request to yield control to MCU
 * @register_i2c_arbitration_event_notifier: Register a notifier for I2C arbitration events
 * @unregister_i2c_arbitration_event_notifier: Unregister a notifier for I2C arbitration events
 */
struct i2c_arbitration_ops {
	void (*take_control_back_ap)(void);
	void (*yield_control_to_mcu)(void);
	int (*register_event_notifier)(void *client_data,
				       int (*callback)(void *client_data,
						       enum i2c_arbitration_event_type));
	void (*unregister_event_notifier)(int (*callback)(void *client_data,
							  enum i2c_arbitration_event_type type));
};

/**
 * i2c_arbitration_register_ops - Register a backend implementation
 * @ops: Pointer to operations structure
 *
 * Returns 0 on success, or negative error code.
 */
int i2c_arbitration_register_ops(const struct i2c_arbitration_ops *ops);

/**
 * i2c_arbitration_unregister_ops - Unregister a backend implementation
 */
void i2c_arbitration_unregister_ops(void);

#endif // _I2C_ARBITRATION_OPS_H_
