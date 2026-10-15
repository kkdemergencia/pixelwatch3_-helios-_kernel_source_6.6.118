/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wear Touch MCU Interface for Pixel wearable devices.
 *
 * Copyright 2024 Google LLC.
 */
#ifndef _WEARABLE_TOUCH_MCU_H_
#define _WEARABLE_TOUCH_MCU_H_

#include <linux/types.h>
#include "wearable_touch_settings_synchronizer.h"

#define MAX_TOUCH_NUM	 2
#define MAX_HEATMAP_SIZE 128

/* Format types, should be aligned with MCU */
enum mcu_touch_format_type {
	/* Used for Touch Service */
	MCU_TOUCH_DATA_ONLY_FORMAT = 0x00,
	/* Only used for Touch Offload Service */
	MCU_TOUCH_HEADER_DATA_FORMAT = 0x01,
};

/* Commands type should be aligned with MCU*/
enum mcu_touch_command_type {
	MCU_TOUCH_COMMAND_GET_DISPLAY_STATE = 0x00,
	MCU_TOUCH_COMMAND_INPUT_EVENT = 0x01,
	MCU_TOUCH_COMMAND_ENABLE_OFFLOAD = 0x02,
	MCU_TOUCH_COMMAND_DISABLE_OFFLOAD = 0x03,
	MCU_TOUCH_COMMAND_POINT_DATA = 0x04,
	MCU_TOUCH_COMMAND_MCU_REQUEST_CONTROL = 0x05,
	MCU_TOUCH_COMMAND_AP_SYNC_INFORMATION_TO_MCU = 0x06,
	MCU_TOUCH_COMMAND_TYPE_MAX,
};
// Wrapper structure of callback for display state
struct mcu_touch_display_state_callback_data {
	char display_mode;
};

//Wrapper structure of callback for input events
struct mcu_touch_input_event_callback_data {
	uint8_t gesture;
	uint8_t touch_cnt;
	uint8_t touch_status[MAX_TOUCH_NUM];
	uint8_t point_id[MAX_TOUCH_NUM];
	uint16_t x_pos[MAX_TOUCH_NUM];
	uint16_t y_pos[MAX_TOUCH_NUM];
	uint16_t pressure[MAX_TOUCH_NUM];
	uint8_t wx[MAX_TOUCH_NUM];
	uint8_t wy[MAX_TOUCH_NUM];
	uint16_t frame_index;
	uint16_t heatmap_len;
	uint8_t heatmap_data[MAX_HEATMAP_SIZE];
} __packed;

//Wrapper structure of callback for offload data
struct mcu_touch_offload_callback_data {
	uint8_t enable;
};

// Wrapper structure of callback for display state
struct mcu_touch_callback_data {
	union {
		// MCU_TOUCH_COMMAND_GET_DISPLAY_STATE
		struct mcu_touch_display_state_callback_data display_data;
		// MCU_TOUCH_COMMAND_POINT_DATA
		struct mcu_touch_input_event_callback_data input_event_data;
		// MCU_TOUCH_COMMAND_MCU_REQUEST_CONTROL
		struct mcu_touch_offload_callback_data offload_data;
	};
};

/*
 * Register callback for dispatcher
 */
extern bool mcu_touch_register_dispatcher(void);

/*
 * Register a callback from MCU touch service,
 * the callback will be triggered when there's a corresponding command type sent from MCU.
 */
extern void
mcu_touch_register_listener(void (*on_message_received)(const struct mcu_touch_callback_data *data),
			    uint32_t mcu_touch_command_type);

/*
 * Unregister the callback from mcu_touch_unregister_listener().
 */
extern void mcu_touch_unregister_listener(
	void (*on_message_received)(const struct mcu_touch_callback_data *data),
	uint32_t mcu_touch_command_type);

extern void wti_mcu_send_key(uint32_t keycode);

extern void wti_mcu_send_offload_settings(bool enable);

extern void wti_mcu_set_message_format(enum mcu_touch_format_type format_type);

extern void wti_mcu_send_settings(struct wearable_touch_ap_settings *settings);

#endif // _WEARABLE_TOUCH_MCU_H_
