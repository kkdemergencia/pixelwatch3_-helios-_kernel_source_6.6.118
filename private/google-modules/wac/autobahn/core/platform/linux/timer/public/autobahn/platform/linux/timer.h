/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google, Inc.
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

#include "autobahn/timer.h"

// Allocates an abn_Timer structure.
//
// Returns 0 and sets the argument to the address of the allocated
// abn_Timer structure for success. Any other value indicates a failure.
int abn_Timer_Create(struct abn_Timer **ppabn_timer);

// Deallocates an abn_Timer structure previously allocated with
// "abn_Timer_Create".
//
// Assumes it is called when the client of the timer has been closed.
void abn_Timer_Destroy(struct abn_Timer *pabn_timer);

