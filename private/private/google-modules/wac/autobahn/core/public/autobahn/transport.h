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

#pragma once

#include "autobahn/preprocessor/util.h"
#include "autobahn/types.h"

ABN_EXTERN_C_START

// The transfer result handling function, should be invoked once per transfer
// unless aborted. After abn_Transport_AbortTransfer returns, this callback
// shall no longer be invoked for a transfer.
//
// This must always be invoked after abn_Transport_InitiateTransfer is called.
// For example a SPI responder may need to defer calling
// abn_Transport_ResultHandler in case the transfer occurred before
// abn_Transport_InitiateTransfer.
//
// This may be invoked in an interrupt context.
typedef void (*abn_Transport_ResultHandler)(void* handler_context,
                                            bool success);

// This is only executed as part of the initialization of Autobahn.
//
// Preconditions:
// - The result_handler must be a valid pointer.
//
// This function is not interrupt safe.
typedef void (*abn_Transport_SetResultHandler)(
    void* transport_handle,
    abn_Transport_ResultHandler result_handler,
    void* handler_context);

// This shall not initiate the transfer.
//
// Preconditions:
// - The result handler must be set to a valid handler.
// - The receive and transmit buffers must be the same size.
// - Both the receive and transmit buffers must be valid pointers.
// - The previous transfer must have either finished through a result handler
//   callback or it must have been explicitly aborted through abort_transfer.
//
// This function is not interrupt safe.
typedef void (*abn_Transport_PrepareTransfer)(void* transport_handle,
                                              uint8_t* tx_data,
                                              uint8_t* rx_data,
                                              size_t size_bytes);

// Kicks off anything necessary locally to start the transfer. Note that this
// may sometimes be a no-op. For example a SPI responder may not have to do
// anything in this function but a UART responder has to initiate the transmit
// to the initiator.
//
// You should expect exact one abn_Transport_ResultHandler callback per
// invocation.
//
// This function is not interrupt safe.
typedef void (*abn_Transport_InitiateTransfer)(void* transport_handle);

// Aborts the transfer. After the return of this function you should no longer
// receive an abn_Transport_ResultHandler callback.
//
// This function should be idempotent, i.e. it should be safe to call even
// if we never prepared nor initiated a transfer.
//
// This function is not interrupt safe.
typedef void (*abn_Transport_AbortTransfer)(void* transport_handle);

struct abn_Transport_Functions {
  abn_Transport_SetResultHandler set_result_handler;
  abn_Transport_PrepareTransfer prepare_transfer;
  abn_Transport_InitiateTransfer initiate_transfer;
  abn_Transport_AbortTransfer abort_transfer;
};

// abn_Transport instances are expected to be fully initialized and enabled
// before Autobahn is started, this is the responsibility of the instantiator.
struct abn_Transport {
  const struct abn_Transport_Functions* functions;
  void* transport_handle;
};

ABN_EXTERN_C_END
