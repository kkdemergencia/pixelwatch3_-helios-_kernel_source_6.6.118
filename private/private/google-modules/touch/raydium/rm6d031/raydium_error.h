/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Raydium TouchScreen driver.
 *
 * Copyright 2025 Google LLC.
 */

/* Raydium INT Timeout Mask & Flags */
#define TOUCH_INT_TIMEOUT_MASK	     0x03
#define TOUCH_NO_TIMEOUT_FLAG	     0x00
#define TOUCH_DOWN_MOVE_TIMEOUT_FLAG 0x01
#define TOUCH_TP_UP_TIMEOUT_FLAG     0x02
#define TOUCH_WAKEUP_TIMEOUT_FLAG    0x03

/* Raydium Touch Mode Mask & Flags */
#define TOUCH_TOUCH_MODE_MASK 0x0C
#define TOUCH_ACTIVE_FLAG     0x00
#define TOUCH_IDLE_FLAG	      0x04
#define TOUCH_GESTURE_FLAG    0x08
#define TOUCH_OTHERS_FLAG     0x0C

/* Raydium Reserved Mask & Flags */
#define TOUCH_RESERVED_MASK 0x30

/* Raydium FW State Mask & Flags */
#define TOUCH_FW_STATE_MASK	 0xC0
#define TOUCH_NORMAL_STATE_FLAG	 0x40
#define TOUCH_INITIAL_STATE_FLAG 0x80

/* Raydium INT Timeout Mask in Error Report */
#define TOUCH_INT_TIMEOUT_DOWN	   0x01
#define TOUCH_INT_TIMEOUT_MOVE	   0x02
#define TOUCH_INT_TIMEOUT_UP	   0x04
#define TOUCH_INT_TIMEOUT_GESTURE  0x08
#define TOUCH_INT_TIMEOUT_RESERVED 0xF0

/* Raydium ALG Status Mask in Error Report */
#define TOUCH_ALG_STATUS_WATER_TAG   0x01
#define TOUCH_ALG_STATUS_PALM_TAG    0x02
#define TOUCH_ALG_STATUS_GESTURE_TAG 0x04
#define TOUCH_ALG_STATUS_REBOOT_TAG  0x08
#define TOUCH_ALG_STATUS_RESERVED    0xF0

/* Raydium Baseline Status Mask in Error Report */
#define TOUCH_BASELINE_STATUS_INITIAL	  0x01
#define TOUCH_BASELINE_STATUS_CALIBRATION 0x02
#define TOUCH_BASELINE_STATUS_NOT_YET	  0x04
#define TOUCH_BASELINE_STATUS_DONE	  0x08
#define TOUCH_BASELINE_STATUS_BS_INFO	  0xF0

/* Raydium Noise Status Mask in Error Report */
#define TOUCH_NOISE_STATUS_NS_TAG   0x01
#define TOUCH_NOISE_STATUS_NS_INFO  0x0E
#define TOUCH_NOISE_STATUS_NS_COUNT 0xF0

/* Raydium Touch Power Status Mask and Flags in Error Report */
#define TOUCH_POWER_STATUS_TOUCH_MODE 0x0F
#define TOUCH_POWER_TOUCH_ACTIVE      0x01
#define TOUCH_POWER_TOUCH_IDLE	      0x02
#define TOUCH_POWER_TOUCH_GESTURE     0x03
#define TOUCH_POWER_TOUCH_OTHERS      0x04

/* Raydium Display Power Status Mask and Flags in Error Report */
#define TOUCH_POWER_STATUS_DISPLAY_MODE 0xF0
#define TOUCH_POWER_DISPLAY_ON		0x10
#define TOUCH_POWER_DISPLAY_IDLE	0x20
#define TOUCH_POWER_DISPLAY_SLEEP_OUT	0x30
#define TOUCH_POWER_DISPLAY_SLEEP_IN	0x40
#define TOUCH_POWER_DISPLAY_DSTB	0x50

/* Raydium Scan Status Mask in Error Report */
#define TOUCH_SCAN_STATUS_START	    0x01
#define TOUCH_SCAN_STATUS_STOP	    0x02
#define TOUCH_SCAN_STATUS_TYPE	    0x0C
#define TOUCH_SCAN_STATUS_HV_FLAG   0x10
#define TOUCH_SCAN_STATUS_AUTO_FLAG 0x20
#define TOUCH_SCAN_STATUS_RESERVED  0xC0
