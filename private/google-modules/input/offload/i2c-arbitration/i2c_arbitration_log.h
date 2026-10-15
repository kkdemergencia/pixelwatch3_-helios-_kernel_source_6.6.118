/* SPDX-License-Identifier: GPL-2.0 */
/*
 * I2C Arbitration module log header file for input offload
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _I2C_ARBITRATION_LOG_H_
#define _I2C_ARBITRATION_LOG_H_

#define LOG_PREFIX     "[i2c-arb]:"
#define LOGE(fmt, ...) pr_err(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)
#define LOGW(fmt, ...) pr_warn(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)
#define LOGI(fmt, ...) pr_info(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)
#define LOGD(fmt, ...) pr_debug(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)
#endif // _I2C_ARBITRATION_LOG_H_
