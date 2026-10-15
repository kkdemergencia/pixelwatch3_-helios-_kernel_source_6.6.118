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

#ifdef __cplusplus

// Macro for inline extern "C" declarations. The following will compile
// correctly for C and C++:
//
//   ABN_EXTERN_C ThisFunctionHasCLinkage(void);
//
#define ABN_EXTERN_C extern "C"

// Macros for opening and closing an extern "C" block. This avoids the need for
// an #ifdef __cplusplus check around the extern "C" { and closing }. Example:
//
//   ABN_EXTERN_C_START
//
//   void FunctionDeclarationForCppAndC(void);
//
//   void AnotherFunctionDeclarationForCppAndC(int, char);
//
//   ABN_EXTERN_C_END
//
#define ABN_EXTERN_C_START extern "C" {
#define ABN_EXTERN_C_END }  // extern "C"

#else  // extern "C" is removed from C code

#define ABN_EXTERN_C
#define ABN_EXTERN_C_START
#define ABN_EXTERN_C_END

#endif  // __cplusplus
