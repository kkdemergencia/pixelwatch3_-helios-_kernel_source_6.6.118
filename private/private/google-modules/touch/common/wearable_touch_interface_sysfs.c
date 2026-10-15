#include "wearable_touch_data_capturer.h"
#include "wearable_touch_log.h"
#include "wearable_touch_interface.h"
#include "wearable_touch_atom.h"

static ssize_t fw_ver_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct wti_fw_version fw_ver = { 0 };
	struct wearable_touch_interface *wti = dev_get_drvdata(dev);

	if (!wti || !wti->common_apis.get_fw_version)
		return scnprintf(buf, PAGE_SIZE, "error: not supported!\n");

	ret = wti->common_apis.get_fw_version(wti->vendor_private_data, &fw_ver);
	if (ret)
		return scnprintf(buf, PAGE_SIZE, "error: %d!\n", ret);

	return scnprintf(buf, PAGE_SIZE, "%s", fw_ver.buffer);
}

static ssize_t hardware_identifier_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret;
	struct wti_hardware_identifier hw_id = { 0 };
	struct wearable_touch_interface *wti = dev_get_drvdata(dev);

	if (!wti || !wti->common_apis.get_hardware_identifier)
		return scnprintf(buf, PAGE_SIZE, "error: not supported!\n");

	ret = wti->common_apis.get_hardware_identifier(wti->vendor_private_data, &hw_id);
	if (ret)
		return scnprintf(buf, PAGE_SIZE, "error: %d!\n", ret);

	return scnprintf(buf, PAGE_SIZE, "%s", hw_id.buffer);
}

static ssize_t display_resolution_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct display_resolution_pixels disp_resolution = { 0 };
	struct wearable_touch_interface *wti = dev_get_drvdata(dev);

	if (!wti || !wti->common_apis.get_display_resolution)
		return scnprintf(buf, PAGE_SIZE, "error: not supported!\n");

	ret = wti->common_apis.get_display_resolution(wti->vendor_private_data, &disp_resolution);
	if (ret)
		return scnprintf(buf, PAGE_SIZE, "error: %d!\n", ret);

	// Use static_assert to avoid buffer overflow in wearable_touch_interface.h
	memcpy(buf, &disp_resolution, sizeof(disp_resolution));
	return sizeof(disp_resolution);
}

static ssize_t grid_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	enum TOUCH_GRID_SPEC grid_spec;
	struct wearable_touch_interface *wti = dev_get_drvdata(dev);

	if (!wti || !wti->common_apis.get_grid_spec)
		return scnprintf(buf, PAGE_SIZE, "error: not supported!\n");

	ret = wti->common_apis.get_grid_spec(wti->vendor_private_data, &grid_spec);
	if (ret)
		return scnprintf(buf, PAGE_SIZE, "error: %d!\n", ret);

	// Use static_assert to avoid buffer overflow in wearable_touch_interface.h
	memcpy(buf, &grid_spec, sizeof(grid_spec));
	return sizeof(grid_spec);
}

static ssize_t dump_touch_frame_data_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct wearable_touch_interface *wti = dev_get_drvdata(dev);

	if(!wti)
		return scnprintf(buf, PAGE_SIZE, "error: not supported!\n");

	return read_frame_buffer(&wti->capturer, buf, PAGE_SIZE);
}

static ssize_t fetch_atoms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t offset = 0, ret;
	struct wearable_touch_interface *wti = dev_get_drvdata(dev);
	struct wti_atom_data *atom_data = &wti->atom_data;

	ret = wti_atom_error_count_report_and_reset(atom_data, buf + offset, PAGE_SIZE - offset);
	if (ret < 0)
		WTI_LOGW("%s: error fetching error count\n", __func__);
	else
		offset += ret;

	ret = wti_atom_gesture_count_report_and_reset(atom_data, buf + offset, PAGE_SIZE - offset);
	if (ret < 0)
		WTI_LOGW("%s: error fetching gesture count\n", __func__);
	else
		offset += ret;

	ret = wti_atom_frame_count_report_and_reset(atom_data, buf + offset, PAGE_SIZE - offset);
	if (ret < 0)
		WTI_LOGW("%s: error fetching frame count\n", __func__);
	else
		offset += ret;

	ret = wti_atom_timeout_count_report_and_reset(atom_data, buf + offset, PAGE_SIZE - offset);
	if (ret < 0)
		WTI_LOGW("%s: error fetching timeout count\n", __func__);
	else
		offset += ret;

	ret = wti_atom_offload_latency_report_and_reset(atom_data, buf + offset,
							PAGE_SIZE - offset);
	if (ret < 0)
		WTI_LOGW("%s: error fetching offload latency\n", __func__);
	else
		offset += ret;

	return offset;
}

static DEVICE_ATTR_RO(fw_ver);
static DEVICE_ATTR_RO(dump_touch_frame_data);
static DEVICE_ATTR_RO(hardware_identifier);
static DEVICE_ATTR_RO(display_resolution);
static DEVICE_ATTR_RO(grid_size);
static DEVICE_ATTR_RO(fetch_atoms);

static struct attribute *wti_attributes[] = {
	&dev_attr_fw_ver.attr,
	&dev_attr_dump_touch_frame_data.attr,
	&dev_attr_hardware_identifier.attr,
	&dev_attr_display_resolution.attr,
	&dev_attr_grid_size.attr,
	&dev_attr_fetch_atoms.attr,
	NULL,
};

static struct attribute_group wti_attr_group = {
	.attrs = wti_attributes,
};

int wti_sysfs_init(struct wearable_touch_interface *wti)
{
	int ret = 0;
	if (!wti || !wti->vendor_dev) {
		WTI_LOGE("%s: wti is NULL or wti_>vendor_dev is NULL!\n", __func__);
		return -EINVAL;
	}
	if (wti->vendor_dev) {
		ret = sysfs_create_link(&wti->dev->kobj, &wti->vendor_dev->kobj, "vendor");
		if (ret) {
			WTI_LOGE("%s: sysfs_create_link() failed for vendor, ret=%d!\n",
			       __func__, ret);
			goto exit_sysfs_create_link_failed;
		}
	}

	ret = sysfs_create_group(&wti->dev->kobj, &wti_attr_group);
	if (ret) {
		WTI_LOGE("%s: sysfs_create_group() failed, ret= %d!\n", __func__, ret);
		goto exit_sysfs_create_group_failed;
	}

	return ret;
exit_sysfs_create_group_failed:
	sysfs_remove_link(&wti->dev->kobj, "vendor");
exit_sysfs_create_link_failed:
	return ret;
}

void wti_sysfs_deinit(struct wearable_touch_interface *wti)
{
	if (wti->vendor_dev) {
		sysfs_remove_group(&wti->dev->kobj, &wti_attr_group);
		sysfs_remove_link(&wti->dev->kobj, "vendor");
	}
}