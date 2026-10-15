/* SPDX-License-Identifier: GPL-2.0+ */
/*****************************************************************************
 *;***************************************************************************
 *; Raydium Semiconductor Corporation.
 *; Design Division V.,
 *; Touch System and Firmware Design Depr II.
 *; Raydium Application Software.
 *;***************************************************************************
 *; Project Name :
 *; File Name      :
 *; Built Date      :
 *;
 *; Copyright 2006 Raydium Semiconductor Corporation.
 *; 2F, No.23, Li-Hsin Rd.,
 *; Hsinchu Science Park, Hsinchu 300,
 *; Taiwan, R.O.C.
 *; Email : @rad-ic.com
 *; Website : www.rad-ic.com
 *; All rights are reserved. Reproduction in whole or part is prohibited
 *; without the prior written consent of the copyright owner.
 *;
 *; COMPANY CONFIDENTIAL
 *;***************************************************************************
 *
 * Revision History:
 * Rev     Date        Author
 *____________________________________________________________________________

 *;---------------------------------------------------------------------------
 ****************************************************************************/
#ifndef __CONFIG_H
#define __CONFIG_H


//  directory is setting in makefile : e.g. /SDK3.0/Firmware/Driver/interface
//
/*****************************************************************************
 *                            GLOBAL MARCO DEFINITION
 *****************************************************************************/
#define MINIMUM(x, y) (((x) < (y)) ? (x) : (y)) //  Compares two parameters, return minimum.
#define MAXIMUM(x, y) (((x) > (y)) ? (x) : (y)) //  Compares two parameters, return maximum.
#define ABS(x) ((x) >= 0 ? (x) : -(x))          // return absolute value

/*****************************************************************************
 *                            GLOBAL FUNCTIONAL DEFINITION
 *****************************************************************************/

//Bootloader
#define BOOTLOADER			1  //1: with bootloader, 0: without bootloader
#define RAD_DONGLE_JIG			0  //1: For raydium test jig
#define SELFTEST			1  //1: For System Selftest, 0:For Dongle Open/Short Tool
#define THIRD_PARTY_TEST_JIG		0 //1: For 3rd-party test jig
#if SELFTEST | THIRD_PARTY_TEST_JIG
#define NXP_COMPILE			0
#if NXP_COMPILE
#include <lpc_types.h>
#endif
#endif

#define DEVICE_RM6DC10			0
#define DEVICE_RM6D030			0
#define DEVICE_RM6D031			1

/*debug define */
#define ENABLE_TEST_TIME_MEASUREMENT		0
#define ENABLE_TEST_GPIO_MEASUREMENT		0
#define ENABLE_TEST_TIME_MEASUREMENT_CC		0
#define ENABLE_SENSOR_IMPACT_TEST		0
#define ENABLE_TEST_SUB_FREQ			0
#define ENABLE_TEST_DO_SLEEP_CC			0
#define ENABLE_RAM_SHRINK			1


#define ENABLE_GPIO_TEST_OUTPUT			0
#endif /* end __CONFIG_H */
/*****************************************************************************
 *                            End Of File
 *****************************************************************************/
