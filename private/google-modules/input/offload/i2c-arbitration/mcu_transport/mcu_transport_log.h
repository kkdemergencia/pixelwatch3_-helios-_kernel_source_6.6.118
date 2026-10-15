/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCU transport module logging interface
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _MCU_TRANSPORT_LOG_H_
#define _MCU_TRANSPORT_LOG_H_

#ifndef LOGE
#define LOGE(fmt, args...) pr_err(fmt, ##args)
#endif /*LOGE*/

#ifndef LOGW
#define LOGW(fmt, args...) pr_warn(fmt, ##args)
#endif /*LOGW*/

#ifndef LOGI
#define LOGI(fmt, args...) pr_info(fmt, ##args)
#endif /*LOGI*/

#ifndef LOGD
#define LOGD(fmt, args...) pr_debug(fmt, ##args)
#endif /*LOGD*/

#endif // _MCU_TRANSPORT_LOG_H_
