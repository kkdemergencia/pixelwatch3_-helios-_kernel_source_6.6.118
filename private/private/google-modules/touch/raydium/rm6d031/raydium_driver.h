/* SPDX-License-Identifier: GPL-2.0+
 *
 * Raydium TouchScreen driver.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.

 * Copyright (c) 2021  Raydium tech Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. or Linaro Ltd. nor the names of
 *    its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 * * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __LINUX_RAYDIUM_H
#define __LINUX_RAYDIUM_H

#include <linux/atomic.h>
#include <linux/pm_qos.h>
#include <wearable_touch_interface.h>

#define RAYDIUM_NAME		"raydium_ts"
#define CONFIG_OF		1
#define COORDS_ARR_SIZE		4
#define I2C_VTG_MIN_UV		1800000
#define I2C_VTG_MAX_UV		1800000
#define RAD_MAIN_VERSION	0x01
#define RAD_MINOR_VERSION	0x01
#define RAD_CUSTOMER_VERSION	0x0100
#define RAD_DEVICE_ID		0xF30300B1

/* IC timing control arguments */
#define RAYDIUM_POWERON_DELAY_USEC	500
#define RAYDIUM_RESET_INTERVAL_MSEC	5
#define RAYDIUM_RESET_RESTORE_USEC	200
#define RAYDIUM_RESET_DELAY_MSEC	100
#define RAYDIUM_POWEROFF_RESET_DELAY_MSEC 10

/* I2C bus secondary address(ID) */
#define RAYDIUM_I2C_EID		(0x5A)
#define RAYDIUM_I2C_NID		(0x39)
#define RAYDIUM_I2C_PDA_CMD	0x66

/* I2C R/W configuration literal */
#define RAYDIUM_I2C_WRITE	I2C_SMBUS_WRITE
#define RAYDIUM_I2C_READ	I2C_SMBUS_READ
#define SYN_I2C_RETRY_TIMES	3
#define MAX_WRITE_PACKET_SIZE	128
#define MAX_READ_PACKET_SIZE	128

/* PDA address and bit definition*/
#define RAD_READ_FT_DATA_CMD	0x2000019C
/* 1byte, disable:0x00 ; enable:0x20*/
#define RAD_GESTURE_STATE_CMD	0x200005F4
#define RAD_GESTURE_DISABLE	0x00
#define RAD_GESTURE_ENABLE	0x20
/* 4bytes, [0]:ready ; [1]:type ; [2]:direction*/
#define RAD_GESTURE_RESULT_CMD	0x200005F0
#define RAD_ENABLE_PDA2		0x04
#define RAD_ENABLE_SI2		0x02

/* Commands of adjusting parameters for edge rejection */
#define RAD_DEAD_ZONE_COMMAND	  0x48
#define RAD_EDGE_ZONE_COMMAND	  0x49
#define RAD_FRAME_COUNTER_COMMAND 0x4A
#define RAD_LANDLOCK_COMMAND	  0x4B

/* PDA literal */
#define MASK_8BIT			0xFF
#define RAD_I2C_PDA_ADDRESS_LENGTH	4
#define PDA_MODE			0x01
#define PDA2_MODE			0x02
#define RAD_I2C_PDA_MODE_DISABLE	0x00
#define RAD_I2C_PDA_MODE_ENABLE		0x80
/* Using byte mode due to data might be not word-aligment */
#define RAD_I2C_PDA_MODE_WORD_MODE	0x40
#define RAD_I2C_PDA_2_MODE_DISABLE	0x20

#define I2C_PDA2_BYTE_MODE		0x03
#define I2C_PDA2_WORD_MODE		0x43

#define RAD_PALM_DISABLE	0x00
#define RAD_PALM_ENABLE		0x01
#define RAD_WAKE_UP		0x02
#define RAD_SWIPE_LEFT		0X03
#define RAD_SWIPE_RIGHT		0X04
#define RAD_SWIPE_UP		0X05
#define RAD_SWIPE_DOWN		0X06
#define RAD_WATER_MODE_ENTER	0x07
#define RAD_WATER_MODE_EXIT	0x08
#define RAD_GESTURE_C		0x03
#define RAD_ENTER_WATER		0x04
#define RAD_LEAVE_WATER		0x05
#define RAYDIUM_TEST_FW		0x80
#define RAYDIUM_TEST_PARA	0x40
#define RAYDIUM_BOOTLOADER	0x20
#define RAYDIUM_FIRMWARE	0x10
#define RAYDIUM_PARA		0x08
#define RAYDIUM_COMP		0x04
#define RAYDIUM_TB_EXE		0x02
#define RAYDIUM_INIT		0x01
#define FAIL			0
#define ERROR			-1
#define SUCCESS			1
#define DISABLE			0
#define ENABLE			1

/* PDA2 setting */
/* Page 0 ~ Page A */
#define MAX_PAGE_AMOUNT		11

/* PDA2 address and setting definition*/
#define RAYDIUM_PDA2_TCH_RPT_STATUS_ADDR	0x00    /* only in Page 0 */
#define RAYDIUM_PDA2_TCH_RPT_ADDR		0x01    /* only in Page 0 */
#define RAYDIUM_PDA2_HOST_CMD_ADDR		0x02    /* only in Page 0 */
#define RAYDIUM_PDA2_TCH_FINGER2_ADDR		0x03    /* only in Page 0 */
#define RAYDIUM_PDA2_PALM_STATUS_ADDR		0x05    /* only in Page 0 */
#define RAYDIUM_PDA2_FW_VERSION_ADDR		0x06    /* only in Page 0 */
#define RAYDIUM_PDA2_ERROR_RPT_ADDR		0x07    /* only in Page 0 */
#define RAYDIUM_PDA2_DISPLAY_INFO_ADDR		0x08    /* only in Page 0 */
#define RAYDIUM_PDA2_PDA_CFG_ADDR		0x09    /* only in Page 0 */
#define RAYDIUM_PDA2_RAWDATA_ADDR		0x0B    /* only in Page 0 */
/* Page 0 ~ Page 9 will be directed to Page 0 */
#define RAYDIUM_PDA2_PAGE_ADDR			0x0A
#define RAYDIUM_PDA2_PAGE_0			0x00
/* temporary switch to PDA once */
#define RAYDIUM_PDA2_ENABLE_PDA			0x0A
/* permanently switch to PDA mode */
#define RAYDIUM_PDA2_2_PDA			(MAX_PAGE_AMOUNT + 2)

/* Bytes to be read or write in each address */
#define RAYDIUM_PDA2_ERROR_RPT_ADDR_DATA_SIZE	 8
#define RAYDIUM_PDA2_DISPLAY_INFO_ADDR_DATA_SIZE 4
#define RAYDIUM_STB_MAX_DATA_SIZE		 4

/* Raydium host cmd */
#define RAYDIUM_HOST_CMD_NO_OP			0x00
#define RAYDIUM_HOST_CMD_DIS_BS_UPDATE		0x20
#define RAYDIUM_HOST_CMD_PWR_SLEEP		0x30
#define RAYDIUM_HOST_CMD_DISPLAY_MODE		0x33
#define RAYDIUM_HOST_CMD_TP_MANUAL_MODE		0x34
#define RAYDIUM_HOST_CMD_REPORT_MODE		0x47
#define RAYDIUM_HOST_CMD_CALIBRATION		0x5C
#define RAYDIUM_HOST_CMD_TP_MODE		0x60
#define RAYDIUM_HOST_CMD_FT_MODE		0x61
#define RAYDIUM_HOST_CMD_FT_FUN_FLAG		0x62

/* Raydium Register define */
#define RAYDIUM_PDA_BOOTVERSION			0x00000080
#define RAYDIUM_PDA_FIRMWAREADDR		0x00000800
#define RAYDIUM_PDA_PARAADDR			0x00008920
#define RAYDIUM_PDA_CCADDR			0x00008A98
#define RAYDIUM_PDA_FIRMWARELENGTH		0x00008120
#define RAYDIUM_PDA_PARALENGTH			0x00000178
#define RAYDIUM_PDA_CRCLENGTH			0x00008294
#define RAYDIUM_PDA__CHANNEL_ADDR		0x00008938
#define RAYDIUM_PDA_SYNCDATA			0x20000200
#define RAYDIUM_PDA_BOOTENG1			0x20000204
#define RAYDIUM_PDA_BOOTENG2			0x20000208
#define RAYDIUM_PDA_BOOTENG3			0x2000020C
#define RAYDIUM_PDA_BOOTENG4			0x20000210
#define RAYDIUM_PDA_BOOTSTATE			0x20000214
#define RAYDIUM_PDA_BOOTMODE			0x20000218
#define RAYDIUM_PDA_FW_VERSION_ADDR		0x200006E0
#define RAYDIUM_PDA_DEADZONE_ADDR		0x200006E8
#define RAYDIUM_PDA_EDGEZONE_ADDR		0x200006EA
#define RAYDIUM_PDA_FRAMECOUNTER_ADDR		0x200006EC
#define RAYDIUM_PDA_LANDLOCK_ADDR		0x200006EE
#define RAYDIUM_PDA_TCH_RPT_STATUS		0x20000290
#define RAYDIUM_PDA_BLKEN			0x40000000
#define RAYDIUM_PDA_BLKRST			0x40000004
#define RAYDIUM_PDA_MISCIER			0x40000014
#define RAYDIUM_PDA_DATA_RAM			0x40000028
#define RAYDIUM_PDA_STB_ADDR			0x5000000C
#define RAYDIUM_PDA_24M				0x50000014
#define RAYDIUM_PDA_PRAM			0x5000005C
#define RAYDIUM_PDA_PID				0x50000064
#define RAYDIUM_PDA_I2CENG			0x50000610
#define RAYDIUM_PDA_FLASHPRO			0x50000624
#define RAYDIUM_PDA_I2CREG			0x50000628
#define RAYDIUM_PDA_PRAMLOCK			0x50000900
#define RAYDIUM_PDA_PRAMTYPE			0x50000904
#define RAYDIUM_PDA_PRAMADDR			0x50000908
#define RAYDIUM_PDA_PRAMLENGTH			0x5000090C
#define RAYDIUM_PDA_FLHADDR			0x50000910
#define RAYDIUM_PDA_FLHCTL			0x50000914
#define RAYDIUM_PDA_BOOTREG			0x50000918
#define RAYDIUM_PDA_FLKEY1			0x50000934
#define RAYDIUM_PDA_FLKEY2			0x50000938
#define RAYDIUM_PDA_FLDATA			0x5000093C
#define RAYDIUM_PDA_PRGCHKSUMENG		0x5000094C
#define RAYDIUM_PDA_PRGCHKSUMADDR		0x50000974
#define RAYDIUM_PDA_PRGCHKSUMRESULT		0x50000978
#define RAYDIUM_PDA_DISPLAY_MODE		0x50001100
#define RAYDIUM_CHK_I2C_CMD			0x500009BC
#define RAYDIUM_REG_GPIO_DEGLITCH		0x50000E1C

#define I2CTB_LOCK				(0x00000001<<6)
#define BOTLR_LOCK				(0x00000001<<5)
#define USEFW_LOCK				(0x00000001<<4)
#define CONFIG_LOCK				(0x00000001<<3)
#define COMP_LOCK				(0x00000001<<2)
#define BASEL_LOCK				(0x00000001<<1)
#define INICO_LOCK				(0x00000001<<0)

/* ['h5000_0904], [32'h0000_0000], Program RAM store type, PRAM_STORE_TYPE */
#define BOTLR_AREA				(0x00000001<<5)
#define USEFW_AREA				(0x00000001<<4)
#define CONFIG_AREA				(0x00000001<<3)
#define COMP_AREA				(0x00000001<<2)
#define BASEL_AREA				(0x00000001<<1)
#define INICO_AREA				(0x00000001<<0)


/* PDA2 literal */
/* entry byte + target page byte */
#define RAYDIUM_I2C_PDA2_PAGE_LENGTH		2


/* Touch report */
#define MAX_TOUCH_NUM			2
#define MAX_REPORT_PACKET_SIZE		229
#define MAX_TCH_STATUS_PACKET_SIZE	LEN_STATUS_SIZE
#define PRESS_MAX			0xFFFF
#define WIDTH_MAX			0xFFFF
#define BYTE_SHIFT			8

/* FW update literal */
#define RAYDIUM_FW_BIN_PATH_LENGTH	256

#define RAD_BOOT_3X_SIZE		0x800
#define RAD_INIT_3X_SIZE		0x24
#define RAD_TB_EXE_3X_SIZE		0x80
#define RAD_FW_3X_SIZE			0x8120
#define RAD_PARA_3X_SIZE		0x174
#define RAD_TESTFW_3X_SIZE		(RAD_FW_3X_SIZE + RAD_PARA_3X_SIZE + 4)
#define RAD_ALLFW_3X_SIZE		0x10DD4
#define RAD_CC_3X_SIZE			0x4E8

#define RAD_CMD_UPDATE_BIN		0x80
#define RAD_CMD_UPDATE_END		0x81
#define RAD_CMD_BURN_FINISH		0x82

/* FT APK literal */
#define RAD_HOST_CMD_POS	0x00
#define RAD_FT_CMD_POS		0x01
#define RAD_FT_CMD_LENGTH	0x04
#define RAD_FT_STATUS0_POS	0x03

/* FT APK data type */
#define RAYDIUM_FT_UPDATE	0x01

/*Raydium system flag*/
#define INT_FLAG	0x01
#define ENG_MODE	0x02
#define NORMAL_MODE	0x00
/* define display mode */
#define ACTIVE_MODE	0x00
#define AMBIENT_MODE	0x01
#define SLEEP_MODE	0x02

#define PANEL_ID_1	0x11
#define PANEL_ID_2	0x02

/*Raydium Report mode*/
#define REPORT_NORMAL_MODE		0x00
#define REPORT_WATER_STREAMING_MODE	0x01
#define REPORT_STREAMING_MODE		0x02

#define FRAME_INDEX_OFFSET_H 21
#define FRAME_INDEX_OFFSET_L 10

#define RAYDIUM_HARD_RESET_RETRY_TIMES	3

/* Enable sysfs */
#define CONFIG_RM_SYSFS_DEBUG

/* Gesture switch */
#define GESTURE_EN

/* Enable FW update */
#define FW_UPDATE_EN

/* Enable PANEL ID MAPPING*/
/* #define FW_MAPPING_EN */
#define MSM_NEW_VER

/* enable SELFTEST */
#define RAD_SELFTEST

#define HEATMAP_DATA_BUFFER_SIZE 128

/* DATA for detecting finger under FDM(Finger Detect Mode) */
#define GP_DATA_BUFFER_SIZE			64
#define ACK_PACKET_SIZE_FOR_STREAMING		1

#define TOUCH_POINT_DATA_OFFSET 4

#define PARA_FW_VERSION_OFFSET	4

#define FW_NAME			"RM6D031.bin"

#define PINCTRL_STATE_ACTIVE	"pmx_ts_active"
#define PINCTRL_STATE_SUSPEND	"pmx_ts_suspend"
#define PINCTRL_STATE_RELEASE	"pmx_ts_release"

#define TOUCH_CLUSTER_GAP_JIFFIES msecs_to_jiffies(5000)
#define REPORT_DELAY_SYSTEM_OFF_JIFFIES msecs_to_jiffies(20)
#define PM_WAKEUP_TIMEOUT 5000

#define CPU_LATENCY_UPPER_BOUND 100
/* Touch Data type */
#define RAYDIUM_TOUCH_BASELINE_DATA	    0x02
#define RAYDIUM_TOUCH_COMPENSATION_CAP_DATA 0x04
#define RAYDIUM_TOUCH_RAW_DATA		    0x08
#define RAYDIUM_TOUCH_DIFF_DATA		    0x10
// Tracks basic information about the most recent
// cluster of touch events. After adding a new member,
// remember to update the log message in function
// `report_touch_stats` in file `raydium_driver.c`
struct raydium_ts_touch_stats {
	atomic_long_t first; // Time of first touch in jiffies
	atomic_long_t last; // Time of last touch in jiffies
	atomic_t ignored_count; // Number of events ignored by oneshot flag
	atomic_t event_count; // Number of events in cluster
	atomic_t fw_i2c_error_count; // Number of fw or I2C error events
	atomic_t wrong_finger_num_count; // Number of wrong finger number events
	atomic_t empty_report_count; // Number of reports with no gesture and finger
	atomic_t suppress_count; // Number of NFC suppress events
	atomic_t reset_count; // Number of reset events
	atomic_t seq_num_dup_count; // Number of duplicate events
	atomic_t motion_count; // Number of motion events
	atomic_t release_count; // Number of release events
	atomic_t out_of_range_count; // Number of out of range events
	atomic_t palm_count; // Number of palm events
	atomic_t wakeup_count; // Number of wakeup events
	atomic_t swipe_left_count; // Number of swipe-left events
	atomic_t swipe_right_count; // Number of swipe-right events
	atomic_t swipe_up_count; // Number of swipe-up events
	atomic_t swipe_down_count; // Number of swipe-down events
	atomic_t water_mode_enter_count; // Number of water mode enter events
	atomic_t water_mode_exit_count; // Number of water mode exit events
	atomic_t mcu_event_count; // Number of events from MCU
};

struct raydium_ts_touch_point_data {
	unsigned int points_amount;
	unsigned int point_id[MAX_TOUCH_NUM];
	unsigned int point_status[MAX_TOUCH_NUM];
	unsigned int x_pos[MAX_TOUCH_NUM];
	unsigned int y_pos[MAX_TOUCH_NUM];
	unsigned int wx[MAX_TOUCH_NUM];
	unsigned int wy[MAX_TOUCH_NUM];
	unsigned int pressure[MAX_TOUCH_NUM];
	uint16_t frame_index;
};

/*
 * This enum is for all the possible reason that the IRQ
 * might be turned on / off. The format should be:
 *
 * REASON_ENTRY(name, can_disable_irq_wake)
 *
 * where `can_disable_irq_wake` is a bool that indicates
 * if this can be a reason that turns on / off IRQ wake.
 *
 * The value is stored in irq_disable_bits for IRQ and
 * irq_wake_disable_bits for IRQ wake, where the bit
 * should be set to 1 if it should be disabled because
 * of the reason in the corresponding bit, vice versa.
 */
#define IRQ_DISABLE_REASON                                                                         \
	{                                                                                          \
		REASON_ENTRY(IRQ_MODE_CONTROL, false) /* Used directly by commands */              \
		REASON_ENTRY(IRQ_FILE_OPS, true) /* Used when open or close */                     \
		REASON_ENTRY(IRQ_TOUCH_LOCK, true) /* Used by sysfs node for touch lock feature */ \
		REASON_ENTRY(TOUCH_OFFLOAD, true) /* For offloading touch to MCU */                \
	}

#define REASON_ENTRY(reason, can_disable_irq_wake) \
	{                                          \
		#reason,                           \
		can_disable_irq_wake,              \
	},
static struct {
	const char *const name;
	const bool can_disable_irq_wake;
} irq_disable_reason_desc[] = IRQ_DISABLE_REASON;
#undef REASON_ENTRY

#define REASON_ENTRY(reason, ...) reason,
enum irq_disable_reason IRQ_DISABLE_REASON;
#undef REASON_ENTRY

#undef IRQ_DISABLE_REASON

/*
 * This enum is for all the possible reason that the IRQ2
 * might be turned on / off.
 */
enum irq2_enable_reason {
	TOUCH_WATCHDOG,
};

/*
 * This enum is for all the possible reason that the IC
 * can enter / exit standby mode (STB). The format should
 * be:
 *
 * REASON_ENTRY(name)
 *
 * The value is stored in is_sleep_bits where the bit
 * should be set to 1 if the IC is in STB because of
 * the reason in the corresponding bit, vice versa.
 */
#define STB_REASON                                                                           \
	{                                                                                    \
		REASON_ENTRY(STB_CMD) /* Used directly by commands */                        \
		REASON_ENTRY(STB_FILE_OPS) /* Used when open or close */                     \
		REASON_ENTRY(STB_TOUCH_LOCK) /* Used by sysfs node for touch lock feature */ \
	}

#define REASON_ENTRY(reason) \
	{                    \
		#reason,     \
	},
static struct {
	const char *const name;
} stb_reason_desc[] = STB_REASON;
#undef REASON_ENTRY

#define REASON_ENTRY(reason) reason,
enum stb_reason STB_REASON;
#undef REASON_ENTRY

#undef STB_REASON

enum factory_test { OFF, FATP1, FATP2, TEST_MAX };

enum raydium_edge_rejection_parameter_types {
	/* Index to access the edge rejection data, like commands and addresses*/
	DEADZONE_IN_PIXELS = 0,
	EDGEZONE_IN_PIXELS,
	FRAMECOUNTER_IN_FRAMES,
	LANDLOCK_IN_PIXELS,
	PARAMETERS_MAX
};
/*The commands of writing edge rejection parameters*/
static const unsigned char edge_rejection_commands[PARAMETERS_MAX] = { RAD_DEAD_ZONE_COMMAND,
								       RAD_EDGE_ZONE_COMMAND,
								       RAD_FRAME_COUNTER_COMMAND,
								       RAD_LANDLOCK_COMMAND };
/*The addresses of accessing Edge rejection parameters*/
static const int edge_rejection_address[PARAMETERS_MAX] = { RAYDIUM_PDA_DEADZONE_ADDR,
							    RAYDIUM_PDA_EDGEZONE_ADDR,
							    RAYDIUM_PDA_FRAMECOUNTER_ADDR,
							    RAYDIUM_PDA_LANDLOCK_ADDR };

struct raydium_ts_edge_rejection_data {
	bool enable;
	/*Edge rejection parameters*/
	unsigned char parameters[PARAMETERS_MAX];
};

struct raydium_report_touch_points {
	unsigned char u8_id;
	signed short i16_x_p;
	signed short i16_y_p;
	signed short i16_z_w;
	signed char i8_x_shape;
	signed char i8_y_shape;
	unsigned char u8_touch_status;
	unsigned char u8_frame_index;
} __packed;

struct raydium_report_finger_data {
	unsigned char u8_seq_num;
	unsigned char u8_touch_points;
	unsigned char u8_gesture_state;
	unsigned char u8_error_state;
	struct raydium_report_touch_points pt[MAX_TOUCH_NUM];
	unsigned char u8_reserve_1;
	unsigned short u16_ic_name;
	unsigned short u16_boot_reg;
	unsigned char u8_edge_flag;
} __packed;

struct raydium_report_error_status {
	unsigned char u8_int_timeout;
	unsigned char u8_touch_count;
	unsigned char u8_timeout_count;
	unsigned char u8_alg_status;
	unsigned char u8_baseline_status;
	unsigned char u8_noise_status;
	unsigned char u8_power_status;
	unsigned char u8_scan_status;
} __packed;

struct raydium_report_gesture_info {
	signed short i16_x_first_p;
	signed short i16_y_first_p;
	signed short i16_peak_value;
	unsigned char u8_duration_count;
	unsigned char u8_reserve;
} __packed;

struct raydium_report_touch_data {
	struct raydium_report_finger_data touch_points;
	struct raydium_report_error_status error_status;
	// self-scan diff data, one cell to scan one
	uint8_t heatmap_data[HEATMAP_DATA_BUFFER_SIZE];
	// grouping-scan diff data, 2 cells to scan one
	uint16_t gp_data[24];
	struct raydium_report_gesture_info gesture_info;
} __packed;

struct raydium_ts_data {
	unsigned int nfc_irq;
	unsigned int irq2;
	unsigned int is_suspend;
	atomic64_t touch_inactive_time;
	atomic_t log_skipped_touch;
	// Used for setting up stimulus and noise test
	atomic_t sensor_test_setup_done;
#ifdef GESTURE_EN
	unsigned int is_palm;
#endif
	unsigned char x_channel;
	unsigned char y_channel;

	struct raydium_ts_platform_data *pdata;
	struct i2c_client *client;
	struct input_dev *input_dev;

	struct mutex lock;
	/* Lock the `sensor_impact_lock` after the general `lock` */
	struct mutex sensor_impact_lock;
	struct irq_desc *irq_desc;
	unsigned long is_sleep_bits;
	unsigned long irq_disable_bits;
	unsigned long irq_wake_disable_bits;
	unsigned long irq2_enable_bits;
	struct mutex irq_lock;
	struct mutex irq2_lock;
	struct raydium_ts_touch_stats stats;
	struct timer_list stats_timer;

#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
	int blank;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif /*end of CONFIG_FB*/

	/*struct regulator *vdd;*/
	struct regulator *vcc_i2c;
	unsigned int fw_version;
	unsigned int id;
	char *vcc_name;
#ifdef MSM_NEW_VER
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_suspend;
	struct pinctrl_state *pinctrl_state_release;
#endif /*end of MSM_NEW_VER*/

	const char *fw_name_dt;
	char customized_fw_name[RAYDIUM_FW_BIN_PATH_LENGTH];

	void *notifier_cookie;
	ktime_t timestamp;
	ktime_t timestamp_real;
	const char *hw_id;
	enum TOUCH_GRID_SPEC grid_spec;
	/* used to protect the assignment of report_input_event function
	 * Has a dependency between raydium_ts_display_data.lock and report_input_event_lock.
	 * We should keep the sequence of accessing lock:
	 *      Lock raydium_ts_display_data.lock -->  lock report_input_event_lock
	 */
	struct mutex report_input_event_lock;
	void (*report_input_event)(uint16_t type, uint16_t code, int32_t value, ktime_t timestamp);
	struct delayed_work delay_touch_event_work;
	bool touch_deep_sleep_enable;
	bool touch_nfc_filter_enable;
	bool streaming_mode_enable;
	enum factory_test factory_test_mode;
	struct mutex factory_test_mode_lock;
	struct completion resume_done;
	struct raydium_ts_edge_rejection_data edge_rejection_data;
	struct pm_qos_request pm_qos_req;
	uint32_t esd_status_addr;
	bool esd_protect_enable;
	bool use_wrapped_i2c;
	bool offload_enable;
	bool offloading;
	bool finish_fw_update;
	/* Has a dependency with g_raydium_ts->lock
	 * Must keep the sequence of accessing lock:
	 *        g_raydium_ts->offload_lock ---> g_raydium_ts->lock
	 * Offload process:
	 *      1. Hold the g_raydium_ts->offload_lock
	 *      2. Assign g_raydium_ts->offloading
	 *      3. call raydium_irq_control(disable)
	 *              --> call disable_irq() which wait the bottom half handler of interrupt
	 *              --> Bottom half handler will hold the g_raydium_ts->lock during processing
	 *              --> the dependency of calling sequence here
	 * The contain is needed to be protected : g_raydium_ts->offloading
	 */
	struct mutex offload_lock;
};

enum offload_request_source {
	MANUAL_CMD,
	TOUCH_SERVICE,
	ARBITRATION_SERVICE,
	OFFLOAD_REQ_MAX,
};

struct raydium_platform_data {
	char *vdd_name;
	int irq_gpio_number;
	int irq2_gpio_number;
	int reset_gpio_number;
	int x_max;
	int y_max;
};

struct raydium_ts_platform_data {
	char *name;
	u32 irqflags;
	u32 irq_gpio;
	u32 irq2_gpio;
	u32 reset_gpio;
	u32 reset_gpio_flags;
	u32 nfc_gpio;
	u32 nfc_active_jiffies;
	u32 nfc_debounce_jiffies;
	u32 x_max;
	u32 y_max;
	u32 x_min;
	u32 y_min;
	u32 hard_rst_dly;
	u32 soft_rst_dly;
	u32 num_max_touches;
	u32 fw_id;
};
enum raydium_ts_driver_report_mode {
	NORMAL,
	QUEUEING,
};
struct raydium_ts_input_event {
	uint16_t type;
	uint16_t code;
	int32_t value;
	ktime_t timestamp;
	struct list_head list;
};

enum INPUT_EVENT_TYPE {
	EV_TIMESTAMP = 0x18,
	EV_SLOT_STATE = 0x19,
	EV_SYNC_FRAME = 0x1a,
};

#pragma pack(push)
#pragma pack(1)
struct raydium_streaming_frame_data {
	unsigned short u16_index;
	unsigned short u16_scan_freq;
	unsigned short u16_scan_rate;
	unsigned int u32_status;
	unsigned short u16_noise_cnt;
	unsigned short u16_gesture_cnt;
	unsigned int u32_checksum;
};
#pragma pack(pop)
struct raydium_ts_devicetree_data {
	struct drm_panel *panel;
};

enum raydium_ts_display_power_state {
	DISPLAY_OFF,
	DISPLAY_INTERACTIVE,
	DISPLAY_AOD,
};

struct raydium_ts_display_data {
	/* Has a dependency between raydium_ts_display_data.lock and report_input_event_lock.
	 * We should keep the sequence of accessing lock:
	 *      Lock raydium_ts_display_data.lock -->  lock report_input_event_lock
	 */
	struct mutex lock;
	enum raydium_ts_display_power_state power_state;
};

/* TODO: Using struct+memcpy instead of array+offset*/
enum raydium_pt_report_status {
	POS_SEQ = 0, /*1:touch, 0:no touch*/
	POS_PT_AMOUNT,
	POS_GES_STATUS,
	POS_FW_STATE,
	LEN_STATUS_SIZE = 4,
};

enum raydium_pt_report_idx {
	POS_PT_ID = 0,
	POS_X_L,
	POS_X_H,
	POS_Y_L,
	POS_Y_H,
	POS_PRESSURE_L,
	POS_PRESSURE_H,
	POS_WX,
	POS_WY,
	POS_STATUS,
	POS_RESERVED,
	LEN_PT_SIZE = 11,
};

enum raydium_err_report_idx {
	POS_INT_TIMEOUT = 0,
	POS_TOUCH_COUNT,
	POS_TIMEOUT_COUNT,
	POS_ALG_STATUS,
	POS_BASELINE_STATUS,
	POS_NOISE_STATUS,
	POS_POWER_STATUS,
	POS_SCAN_STATUS,
};

enum raydium_touch_status {
	TOUCH_NONE = 0,
	TOUCH_PRESS,
	TOUCH_RELEASE,
	TOUCH_MOVE,
	TOUCH_COVER,
	TOUCH_COVER_RELEASE,
	TOUCH_SHORTCLICK
};

enum raydium_log_level {
	LOG_NONE = 0,
	LOG_ALERT,
	LOG_ERR,
	LOG_WARNING,
	LOG_INFO,
	LOG_DEBUG = 5
};

// Interrupt handler for production and testing
extern irqreturn_t raydium_ts_top_handler(int irq, void *data);
extern irqreturn_t raydium_ts_top_handler2(int irq, void *data);
extern irqreturn_t raydium_ts_interrupt(int irq, void *dev_id);
extern irqreturn_t raydium_ts_interrupt2(int irq, void *dev_id);
extern irqreturn_t raydium_ts_interrupt_test(int irq, void *dev_id);
extern irqreturn_t raydium_ts_interrupt2_test(int irq, void *dev_id);

extern int raydium_mem_table_setting(void);
extern int wait_fw_state(struct i2c_client *client, unsigned int u32_addr,
			 unsigned int u32_state, unsigned long u32_delay_us,
			 unsigned short u16_retry);
extern int wait_irq_state(struct i2c_client *client,
			  unsigned int u32_retry_time,
			  unsigned int u32_delay_us);
extern void raydium_irq_control(bool enable, enum irq_disable_reason reason);
extern int raydium_hw_reset_helper_func(struct i2c_client *client);
extern void raydium_stb_control(bool enable, enum stb_reason reason);

extern int raydium_i2c_mode_control(struct i2c_client *client,
				    unsigned char u8_mode);
extern int raydium_i2c_pda_read(struct i2c_client *client,
				unsigned int u32_addr, unsigned char *u8_r_data,
				unsigned short u16_length);
extern int raydium_i2c_pda_write(struct i2c_client *client,
				 unsigned int u32_addr, unsigned char *u8_w_data,
				 unsigned short u16_length);
extern int handle_i2c_pda_write(struct i2c_client *client,
				unsigned int u32_addr, unsigned char *u8_w_data,
				unsigned short u16_length);
extern int handle_i2c_pda_read(struct i2c_client *client,
			       unsigned int u32_addr, unsigned char *u8_r_data,
			       unsigned short u16_length);
extern int raydium_i2c_pda2_read(struct i2c_client *client,
				 unsigned char u8_addr,
				 unsigned char *u8_r_data,
				 unsigned short u16_length);
extern int raydium_i2c_pda2_write(struct i2c_client *client,
				  unsigned char u8_addr,
				  unsigned char *u8_w_data,
				  unsigned short u16_length);
extern int raydium_i2c_pda2_set_page(struct i2c_client *client,
				     unsigned int is_suspend,
				     unsigned char u8_page);
extern int raydium_i2c_write_pda_via_pda2(struct i2c_client *client,
		unsigned int u32_addr, unsigned char *u8_w_data,
		unsigned short u16_length);
extern int raydium_i2c_read_pda_via_pda2(struct i2c_client *client,
		unsigned int u32_addr, unsigned char *u8_r_data,
		unsigned short u16_length);
extern unsigned char raydium_i2c_init(void);
extern unsigned char raydium_selftest_stop_mcu(struct i2c_client *client);
extern int raydium_burn_comp(struct i2c_client *client);
extern int raydium_burn_fw(struct i2c_client *client);

extern int raydium_load_test_fw(struct i2c_client *client);
extern int raydium_fw_update_init(unsigned short u16_i2c_data);
extern int raydium_fw_update_check(unsigned int u32_check_version);
extern int raydium_i2c_pda_set_address(unsigned int u32_address,
				       unsigned char u8_mode);
extern void raydium_mem_table_init(void);
extern int raydium_id_init(unsigned char u8_type);
extern int raydium_load_fw(struct i2c_client *client, unsigned char u8_type);
extern void firmware_update_cb(const struct firmware *fw, void *ctx);
extern int raydium_i2c_touch_sleep_locked(bool enable);
extern int raydium_adjust_edge_rejection_locked(enum raydium_edge_rejection_parameter_types type,
						unsigned char parameter);
extern int raydium_read_touchdata_wrapping_locked(
	struct raydium_report_touch_data *report_touch_data);
extern int raydium_read_touchdata_locked(struct raydium_report_touch_data *report_touch_data);

#ifdef RAD_SELFTEST
extern int raydium_do_selftest(struct raydium_ts_data *ts, char *p_i8_buf, short size_limit);
extern int raydium_do_sensor_test_setup(struct i2c_client *client);
extern int raydium_do_stimulus_test(struct i2c_client *client, char *p_i8_buf,
				    unsigned int size_limit);
extern int raydium_do_noise_test(struct i2c_client *client, char *p_i8_buf,
				 unsigned int size_limit);
#endif
int raydium_esd_check(void);

extern int32_t raydium_set_offload(bool enable, enum offload_request_source source);

extern struct attribute *raydium_attributes[];
extern const struct attribute_group raydium_attr_group;

extern unsigned char g_u8_raydium_flag;
extern unsigned char g_u8_addr;
extern unsigned char g_u8_i2c_mode;
extern unsigned char g_u8_upgrade_type;
extern unsigned char g_u8_raw_data_type;
extern unsigned int g_u32_raw_data_len;    /* 128 bytes*/
extern unsigned char g_u8_raw_data_pure;
extern unsigned int g_u32_length;
extern unsigned long g_u32_addr;
extern unsigned char *g_rad_fw_image, *g_rad_init_image, *g_rad_tb_exe_image;
extern unsigned char *g_rad_boot_image, *g_rad_para_image;
extern unsigned char *g_rad_testfw_image, *g_rad_testpara_image;
extern unsigned char g_u8_table_setting, g_u8_table_init;
extern unsigned int g_u32_driver_version;
extern unsigned char g_u8_resetflag;
extern unsigned char g_rad_cc_image[RAD_CC_3X_SIZE];
extern unsigned char g_u8_cc_ready;
extern unsigned char g_u8_cc_info;
extern struct raydium_ts_data *g_raydium_ts;
extern unsigned char g_u8_log_level;
extern unsigned char g_u8_i2c_retry_cnt;
extern unsigned char g_u8_report_mode;

#define LOG_PREFIX     "[raydium-touch]:"
#define LOGD(fmt, ...) pr_debug(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)
#define LOGI(fmt, ...) pr_info(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)
#define LOGW(fmt, ...) pr_warn(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)
#define LOGE(fmt, ...) pr_err(pr_fmt(LOG_PREFIX fmt), ##__VA_ARGS__)

#define LOG_STAT_PREFIX	   "[raydium-stat] "
#define LOG_STAT(fmt, ...) pr_info(pr_fmt(LOG_STAT_PREFIX fmt), ##__VA_ARGS__)

#endif  /*__LINUX_RAYDIUM_H*/

