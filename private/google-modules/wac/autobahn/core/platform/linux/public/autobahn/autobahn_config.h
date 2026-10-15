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

#ifndef _AUTOBAHN_CONFIG_H
#define _AUTOBAHN_CONFIG_H

#if defined(CONFIG_AUTOBAHN_FRAME_SIZE_BYTES) && CONFIG_AUTOBAHN_FRAME_SIZE_BYTES
  #define ABN_FRAME_SIZE CONFIG_AUTOBAHN_FRAME_SIZE_BYTES
#else
  #warning "Kconfig option AUTOBAHN_FRAME_SIZE_BYTES not defined."
#endif

#if defined(ABN_FRAME_SIZE) && (ABN_FRAME_SIZE > 264)
  #define ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED 1
#else
  #define ABN_FRAME_PAYLOAD_ON_TWO_BYTES_ENABLED 0
#endif

#endif /* _AUTOBAHN_CONFIG_H */
