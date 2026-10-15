/* SPDX-License-Identifier: GPL-2.0 */
/*
 * I2C Arbitration Common Interface header
 *
 * Copyright 2026 Google LLC.
 */
#ifndef _I2C_ARBITRATION_MODULE_H_
#define _I2C_ARBITRATION_MODULE_H_

/**
 * @enum i2c_arbitration_event_type
 * @brief Defines the owners of the I2C bus during arbitration.
 */
enum i2c_arbitration_event_type {
	I2C_OWNER_AP,
	I2C_OWNER_MCU,
};

/**
 * @brief Request to take control of the I2C bus back to AP.
 */
void offloading_request_take_control_back_ap(void);

/**
 * @brief Request to yield control of the I2C bus to MCU.
 */
void offloading_request_yield_control_to_mcu(void);

/**
 * @brief Register a callback for I2C arbitration events.
 * @param client_data Data to be passed to the callback
 * @param callback Callback function to be called when an I2C arbitration event occurs
 * @return 0 on success, or negative error code
 */
int register_i2c_arbitration_event_notifier(void *client_data,
					    int (*callback)(void *client_data,
							    enum i2c_arbitration_event_type type));

void unregister_i2c_arbitration_event_notifier(
	int (*callback)(void *client_data, enum i2c_arbitration_event_type type));

#endif // _I2C_ARBITRATION_MODULE_H_
