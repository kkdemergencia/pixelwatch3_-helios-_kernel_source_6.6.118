#include "linux/printk.h"
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include "wearable_touch_data_capturer.h"
#include "wearable_touch_log.h"
#include "wearable_touch_interface.h"

static int is_full(struct touch_data_capturer *capturer)
{
	return (capturer->rdx == ((capturer->wdx + 1) % TOUCH_FRAME_BUFFERS_SIZE));
}

static int is_empty(struct touch_data_capturer *capturer)
{
	return (capturer->rdx == capturer->wdx);
}

static int setup_frame_size(struct touch_frame *frame, enum TOUCH_GRID_SPEC spec)
{
	if (!frame) {
		WTI_LOGE("%s: Failed to setup frame size due to null ptr\n", __func__);
		return 1;
	}
	frame->frame_size = get_frame_size(spec);
	return frame->frame_size <= 0;
}

static int setup_grid_buf_size(struct touch_frame *frame, enum TOUCH_GRID_SPEC spec)
{
	if (!frame) {
		WTI_LOGE("%s: Failed to setup grid buf size due to null ptr\n", __func__);
		return 1;
	}
	frame->grid_buf_size = get_grid_buf_size(spec);
	return frame->grid_buf_size <= 0;
}

void reset_capturer(struct touch_data_capturer *capturer) {
	capturer->rdx = 0;
	capturer->wdx = 0;
}

static inline void reset_frame_buffer(struct touch_frame_buffer *frame_buf)
{
	frame_buf->num_frames = 0;
}


static int init_frame(struct touch_frame *frame, enum TOUCH_GRID_SPEC spec)
{
	int ret;

	reset_frame_metadata(&frame->metadata);

	ret = setup_grid_buf_size(frame, spec);
	if (ret) {
		WTI_LOGE("%s: Failed to set up grid buf size for touch data capturer\n", __func__);
		return ret;
	}

	ret = setup_frame_size(frame, spec);
	if (ret) {
		WTI_LOGE("%s: Failed to set up frame size for touch data capturer\n", __func__);
		return ret;
	}

	kfree(frame->grid);
	frame->grid = kzalloc(frame->grid_buf_size, GFP_KERNEL);
	if (!frame->grid) {
		WTI_LOGE("%s: failed to allocate mem for grid data\n", __func__);
		return -1;
	}
	return 0;
}

int init_capturer(struct touch_data_capturer *capturer, enum TOUCH_STREAMING_MODE mode,
		enum TOUCH_GRID_SPEC spec)
{
	int ret = 0;

	WTI_LOGI("%s: Initialize touch data capturer\n", __func__);
	mutex_lock(&capturer->lock);

	reset_capturer(capturer);

	capturer->touch_streaming_mode = mode;
	capturer->touch_grid_spec = spec;
	capturer->last_frame_touches_reported = false;

	reset_frame_buffer(&capturer->building_frame_buf);
	ret = init_frame(&capturer->building_frame, spec);
	if (ret) {
		WTI_LOGE("%s: Failed to initialize frame data\n", __func__);
		mutex_unlock(&capturer->lock);
		return ret;
	}

	mutex_unlock(&capturer->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(init_capturer);

/*
 * Note that the FW should behave as follow, starting from raydium FW v5
 *
 * 1. WATER_MODE_ENTER may be reported with motion data
 * 2. WATER_MODE_EXIT will not be reported with motion data
 * 3. If WATER_MODE_ENTER / EXIT and tap-to-wake/swipe-to-wake/palm gesture happens
 *    in the same frame, the gesture will be reported first
 */
static bool is_end_touch_event(struct touch_frame *frame, bool last_frame_touches_reported)
{
	// When the last one is PRESS / MOVE motion event AND this one is gesture or RELEASE,
	// we should report it
	bool motionEnd = last_frame_touches_reported && !frame->metadata.touches_reported;

	// In the case of `WATER_MODE_ENTER` with PRESS / MOVE motion event, we should not report it
	bool gestureEnd =
		!frame->metadata.touches_reported &&
		(frame->metadata.gesture_reported != TOUCH_GESTURE_INVALID);

	return motionEnd || gestureEnd;
}

void capturer_insert_frame_index(struct touch_data_capturer *capturer, uint16_t frame_index)
{
	if (capturer == NULL) {
		WTI_LOGE("%s: Failed to insert frame index due to capturer null\n", __func__);
		return;
	}
	capturer->building_frame.metadata.frame_index = frame_index;
}
EXPORT_SYMBOL_GPL(capturer_insert_frame_index);

void capturer_insert_gesture(struct touch_data_capturer *capturer,
			     enum TOUCH_GESTURE gesture_reported)
{
	if (capturer == NULL) {
		WTI_LOGE("%s: Failed to insert gesture due to capturer null\n", __func__);
		return;
	}
	capturer->building_frame.metadata.gesture_reported = gesture_reported;
}
EXPORT_SYMBOL_GPL(capturer_insert_gesture);

void capturer_insert_num_touches(struct touch_data_capturer *capturer, uint8_t num_touches)
{
	if (capturer == NULL) {
		WTI_LOGE("%s: Failed to insert num touches due to capturer null\n", __func__);
		return;
	}
	capturer->building_frame.metadata.num_touches = num_touches;
}
EXPORT_SYMBOL_GPL(capturer_insert_num_touches);

void capturer_insert_touch_coordinates(struct touch_data_capturer *capturer, int16_t x, int16_t y,
				       int16_t id)
{
	if (capturer == NULL) {
		WTI_LOGE("%s: Failed to insert touch coordinates due to capturer null\n", __func__);
		return;
	}
	if (id >= MAX_FINGERS_TRACKED) {
		WTI_LOGE("%s: touch id is out of range of max fingers tracked, id=%d, x=%d, y=%d\n",
		     __func__, id, x, y);
			 return;
	}
	capturer->building_frame.metadata.touch_coordinates[id].x = x;
	capturer->building_frame.metadata.touch_coordinates[id].y = y;
	capturer->building_frame.metadata.touches_reported = true;
}
EXPORT_SYMBOL_GPL(capturer_insert_touch_coordinates);

void capturer_insert_grid_data(struct touch_data_capturer *capturer, void *grid,
			       size_t grid_buf_size)
{
	if (capturer == NULL) {
		WTI_LOGE("%s: Failed to insert grid data due to capturer null\n", __func__);
		return;
	}

	if (grid_buf_size != capturer->building_frame.grid_buf_size) {
		WTI_LOGE("%s: buffer size is not consistent, ext buf size=%zu int buf size=%zu\n",
		     __func__, grid_buf_size, capturer->building_frame.grid_buf_size);
		return;
	}
	memcpy(capturer->building_frame.grid, grid, grid_buf_size);
}
EXPORT_SYMBOL_GPL(capturer_insert_grid_data);

static int insert_frame(struct touch_frame_buffer *frame_buf, struct touch_frame *frame,
			int64_t irq_timestamp)
{
	uint32_t offset;

	if ((frame->frame_size + frame_buf->num_frames * frame->frame_size) >
	    TOUCH_FRAME_BUFFER_SIZE_BYTES) {
		WTI_LOGE("%s: Failed to insert frame into frame buffer\n", __func__);
		return 1;
	}
	offset = frame_buf->num_frames * frame->frame_size;
	memcpy(&frame_buf->buffer[offset], (void *)&frame->metadata,
	       sizeof(struct touch_frame_metadata));
	memcpy(&frame_buf->buffer[offset] + sizeof(struct touch_frame_metadata),
	       frame->grid, frame->grid_buf_size);

	if (frame_buf->num_frames == 0)
		frame_buf->timestamp.ktime_real_ns_start = irq_timestamp;

	frame_buf->num_frames++;
	return 0;
}

/* Helper function to create touch_frame_buffer when there's an interrupt triggered in touch driver.
 * Should not be called simultaneously with other `capturer_insert_xxx` function. Thus, use a lock
 * or work queue with queued size equals to 1 to avoid race condition.
 */
int capturer_frame_finish(struct touch_data_capturer *capturer, int64_t irq_timestamp)
{
	int ret;
	struct touch_frame *building_frame;
	struct touch_frame_buffer *building_frame_buf;

	if (!capturer) {
		WTI_LOGE("%s: capturer is NULL\n", __func__);
		return 1;
	}

	building_frame = &capturer->building_frame;
	building_frame_buf = &capturer->building_frame_buf;

	ret = insert_frame(building_frame_buf, building_frame, irq_timestamp);
	if (ret) {
		WTI_LOGE("%s: Failed to insert frame\n", __func__);
		return ret;
	}

	// Insert the frame buffer into the circular buffer if frame buffer is full.
	// Prepare the next frame buffer.
	if (building_frame_buf->num_frames == MAX_BUFFERED_FRAMES ||
	    is_end_touch_event(building_frame, capturer->last_frame_touches_reported)) {
		insert_frame_buffer(capturer, irq_timestamp);
		reset_frame_buffer(building_frame_buf);
	}

	capturer->last_frame_touches_reported = building_frame->metadata.touches_reported;
	reset_frame_metadata(&building_frame->metadata);
	return 0;
}
EXPORT_SYMBOL_GPL(capturer_frame_finish);

int insert_frame_buffer(struct touch_data_capturer *capturer, int64_t final_irq_timestamp)
{
	struct touch_frame_buffer *target_frame_buf;

	mutex_lock(&capturer->lock);
	if ((capturer == NULL) || (capturer->building_frame_buf.num_frames == 0)) {
		mutex_unlock(&capturer->lock);
		WTI_LOGE("%s: Failed to insert framebuffer into touch data capturer\n", __func__);
		return 1;
	}

	if (is_full(capturer))
		capturer->rdx = (capturer->rdx + 1) % TOUCH_FRAME_BUFFERS_SIZE;

	capturer->building_frame_buf.timestamp.ktime_real_ns_end = final_irq_timestamp;
	target_frame_buf = &capturer->frame_buffers[capturer->wdx];
	memcpy(target_frame_buf, &capturer->building_frame_buf, sizeof(struct touch_frame_buffer));
	capturer->wdx = (capturer->wdx + 1) % TOUCH_FRAME_BUFFERS_SIZE;
	mutex_unlock(&capturer->lock);
	sysfs_notify(&capturer->wti_dev->kobj, NULL, DUMP_DATA_FILE_NAME);
	return 0;
}

static int pop_frame_buffer_locked(struct touch_data_capturer *capturer) {
  if (((capturer == NULL) || is_empty(capturer))) {
    return 1;
  }
  capturer->rdx = (capturer->rdx + 1) % TOUCH_FRAME_BUFFERS_SIZE;
  return 0;
}
/* Read the frame buffer and then pop it*/
int read_frame_buffer(struct touch_data_capturer *capturer, void *out_buf, uint16_t out_buf_size)
{
	int ret = 0;

	if (mutex_lock_interruptible(&capturer->lock)) {
		return -ERESTARTSYS;
	}

	if (((capturer == NULL) || is_empty(capturer)) ||
	    (out_buf == NULL) || (out_buf_size < sizeof(struct touch_frame_buffer))) {
		mutex_unlock(&capturer->lock);
		WTI_LOGE("%s: Failed to read framebuffer from touch data capturer\n", __func__);
		return -EAGAIN;
	}
	memcpy(out_buf, &capturer->frame_buffers[capturer->rdx], sizeof(struct touch_frame_buffer));
	ret = pop_frame_buffer_locked(capturer);
	if (ret) {
		mutex_unlock(&capturer->lock);
		WTI_LOGE("%s: Failed to pop framebuffer from touch data capturer\n", __func__);
		return -EAGAIN;
	}
	mutex_unlock(&capturer->lock);
	return sizeof(struct touch_frame_buffer);
}

uint16_t get_frame_size(enum TOUCH_GRID_SPEC spec)
{
	switch (spec) {
	case TOUCH_GRID_SPEC_INT16_8X8:
		return get_grid_buf_size(spec) + sizeof(struct touch_frame_metadata);
	case TOUCH_GRID_SPEC_INVALID:
		return 0;
	default:
		WTI_LOGE("%s: Received unexpected grid spec: %d\n", __func__, spec);
		return 0;
	}
}

uint16_t get_grid_buf_size(enum TOUCH_GRID_SPEC spec)
{
	switch (spec) {
	case TOUCH_GRID_SPEC_INT16_8X8:
		return 8 * 8 * 2;
	case TOUCH_GRID_SPEC_INVALID:
		return 0;
	default:
		WTI_LOGE("%s: Received unexpected grid spec: %d\n", __func__, spec);
		return 0;
	}
}




void reset_frame_metadata(struct touch_frame_metadata *metadata)
{
	uint8_t i;

	metadata->frame_index = 0;
	metadata->gesture_reported = 0;
	metadata->num_touches = 0;
	metadata->touches_reported = 0;
	for (i = 0; i < MAX_FINGERS_TRACKED; i++) {
		metadata->touch_coordinates[i].x = 0;
		metadata->touch_coordinates[i].y = 0;
	}
}

