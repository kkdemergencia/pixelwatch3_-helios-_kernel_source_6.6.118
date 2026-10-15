#ifndef _MCU_TOUCH_H
#define _MCU_TOUCH_H

/* Commands type should be aligned with MCU*/
enum nanohub_touch_command_type {
	NANOHUB_TOUCH_COMMAND_LISTEN_ON_DISPLAY_STATE,
	NANOHUB_TOUCH_COMMAND_TYPE_MAX,
};

// Wrapper structure for callback register
struct touch_callback_data {
	union {
		char display_mode; // NANOHUB_TOUCH_COMMAND_LISTEN_ON_DISPLAY_STATE
	};
};
/*
 * Register a callback from MCU touch service,
 * the callback will be triggered when there's a corresponding command type sent from MCU.
 */
extern void
nanohub_touch_register_listener(void (*on_message_received)(const struct touch_callback_data *data),
				uint32_t nanohub_touch_command_type);

/*
 * Unregister the callback from nanohub_touch_unregister_listenr().
 */
extern void nanohub_touch_unregister_listener(
	void (*on_message_received)(const struct touch_callback_data *data),
	uint32_t nanohub_touch_command_type);
#endif
