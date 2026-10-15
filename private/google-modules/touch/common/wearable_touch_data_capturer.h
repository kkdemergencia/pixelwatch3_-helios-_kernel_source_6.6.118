#ifndef _WEARABLE_TOUCH_DATA_CAPTURER_
#define _WEARABLE_TOUCH_DATA_CAPTURER_

#include <linux/types.h>
#include <linux/string.h>
#include <linux/mutex.h>
#define TOUCH_FRAME_BUFFER_SIZE_BYTES	1024
#define MAX_BUFFERED_FRAMES		6
#define MAX_FINGERS_TRACKED		2
#define TOUCH_FRAME_BUFFERS_SIZE	20
#define DUMP_DATA_FILE_NAME	"dump_touch_frame_data"

enum TOUCH_STREAMING_MODE {
	TOUCH_STREAMING_MODE_INVALID = 0,
	TOUCH_STREAMING_MODE_DIFF_ON_EVENTS = 1,
	TOUCH_STREAMING_MODE_DIFF_CONTINUOUS = 2,
};

enum TOUCH_GRID_SPEC {
	TOUCH_GRID_SPEC_INVALID = 0,
	TOUCH_GRID_SPEC_INT16_8X8 = 1,
};
static_assert(PAGE_SIZE >= sizeof(enum TOUCH_GRID_SPEC));

enum TOUCH_GESTURE {
	TOUCH_GESTURE_INVALID = 0,
	TOUCH_GESTURE_PALM_SCREEN_OFF = 1,
	TOUCH_GESTURE_TOUCH_TO_WAKE = 2,
	TOUCH_GESTURE_SWIPE_TO_WAKE_LEFT = 3,
	TOUCH_GESTURE_SWIPE_TO_WAKE_RIGHT = 4,
	TOUCH_GESTURE_SWIPE_TO_WAKE_UP = 5,
	TOUCH_GESTURE_SWIPE_TO_WAKE_DOWN = 6,

	/* Following values work as flags */
	TOUCH_FLAG_WATER_MODE_ENTER = 7,
	TOUCH_FLAG_WATER_MODE_EXIT = 8,
};

struct touch_coordinates {
	int16_t x;
	int16_t y;
} __packed;

struct display_resolution_pixels {
	int16_t x;
	int16_t y;
} __packed;
static_assert(PAGE_SIZE >= sizeof(struct display_resolution_pixels));

// Populate with ktime_get_real
struct touch_buffer_timestamp {
	int64_t ktime_real_ns_start;
	int64_t ktime_real_ns_end;
} __packed;

struct touch_frame_metadata {
	uint16_t frame_index;
	enum TOUCH_GESTURE gesture_reported;
	bool touches_reported;
	uint8_t num_touches;
	struct touch_coordinates touch_coordinates[MAX_FINGERS_TRACKED];
} __packed;

/*
 * grid data should be looks like, e.g. x_channel = 8, y_channel = 8
 *                x_channel
 *  y_channel     X X X X X X X X
 *                X X X X X X X X
 *                X X X X X X X X
 *                X X X X X X X X
 *                X X X X X X X X
 *                X X X X X X X X
 *                X X X X X X X X
 *                X X X X X X X X
 * the bytes of X should depends on the TouchGridSpec
 */
struct touch_frame {
	struct touch_frame_metadata metadata;
	size_t grid_buf_size;
	size_t frame_size;
	void *grid;
} __packed;

struct touch_frame_buffer {
	struct touch_buffer_timestamp timestamp;
	uint8_t num_frames;
	uint8_t buffer[TOUCH_FRAME_BUFFER_SIZE_BYTES];
} __packed;

struct touch_data_capturer {
	struct device *wti_dev;
	struct mutex lock;
	enum TOUCH_GRID_SPEC touch_grid_spec;
	struct touch_frame building_frame;
	struct touch_frame_buffer building_frame_buf;
	struct touch_frame_buffer frame_buffers[TOUCH_FRAME_BUFFERS_SIZE];
	enum TOUCH_STREAMING_MODE touch_streaming_mode;
	bool last_frame_touches_reported;
	uint8_t rdx;
	uint8_t wdx;
};

void reset_capturer(struct touch_data_capturer *capturer);
void reset_frame_metadata(struct touch_frame_metadata *metadata);
int init_capturer(struct touch_data_capturer *capturer, enum TOUCH_STREAMING_MODE mode,
		  enum TOUCH_GRID_SPEC spec);
void capturer_insert_frame_index(struct touch_data_capturer *capturer, uint16_t frame_index);
void capturer_insert_gesture(struct touch_data_capturer *capturer,
			     enum TOUCH_GESTURE gesture_reported);
void capturer_insert_num_touches(struct touch_data_capturer *capturer, uint8_t num_touches);
void capturer_insert_touch_coordinates(struct touch_data_capturer *capturer, int16_t x, int16_t y,
				       int16_t id);
void capturer_insert_grid_data(struct touch_data_capturer *capturer, void *grid,
			       size_t grid_buf_size);
int capturer_frame_finish(struct touch_data_capturer *capturer, int64_t irq_timestamp);
int insert_frame_buffer(struct touch_data_capturer *capturer, int64_t final_irq_timestamp);
int read_frame_buffer(struct touch_data_capturer *capturer, void *out_buf, uint16_t out_buf_size);
uint16_t get_frame_size(enum TOUCH_GRID_SPEC spec);
uint16_t get_grid_buf_size(enum TOUCH_GRID_SPEC spec);


#endif /*_WEARABLE_TOUCH_DATA_CAPTURER_*/