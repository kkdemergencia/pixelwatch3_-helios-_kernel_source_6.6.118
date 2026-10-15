/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Google LLC */

#include "google-kamino.h"
#include "google-kamino-event-faker.h"

#include <linux/version.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

void google_kamino_event_faker_init(struct kamino *kamino)
{
	struct kamino_event_faker *faker = &kamino->event_faker;
	mutex_init(&faker->lock);
}

void google_kamino_event_faker_deinit(struct kamino *kamino)
{
	/* Note: google-kamino-core.c cleans up all debugfs entries */
	mutex_destroy(&kamino->event_faker.lock);
}

bool google_kamino_event_faker_enabled(struct kamino *kamino)
{
	struct kamino_event_faker *faker = &kamino->event_faker;
	bool enabled;
	mutex_lock(&faker->lock);
	enabled = faker->enabled;
	mutex_unlock(&faker->lock);

	return enabled;
}

#ifdef CONFIG_DEBUG_FS
static int enabled_get(void *data, u64 *val)
{
	*val = google_kamino_event_faker_enabled((struct kamino *)data);
	return 0;
}

static int enabled_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_event_faker *faker = &kamino->event_faker;
	const bool enable = val;

	mutex_lock(&faker->lock);

	if (faker->enabled == enable)
		goto out;

	if (enable) {
		google_kamino_disable_irqs(kamino);
	} else {
		google_kamino_enable_irqs(kamino);
		google_kamino_on_new_status(kamino);
	}

	faker->enabled = enable;

out:
	mutex_unlock(&faker->lock);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(enabled_fops, enabled_get, enabled_set, "%llu\n");

#define INJECTED_EVENT_NUM_ARGS 2
#define STATUS_STR_MAX_LEN 1
#define IN_REAUTH_MAX_LEN 1
#define SEPARATOR_LEN 1

/* Add one byte for null terminator */
#define INJECTED_EVENT_SIZE_MAX (STATUS_STR_MAX_LEN \
				+ SEPARATOR_LEN \
				+ IN_REAUTH_MAX_LEN \
				+ 1)
static ssize_t inject_event_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct kamino *kamino = (struct kamino *)file->private_data;
	struct kamino_event_faker *faker = &kamino->event_faker;
	int ret;
	char mem[INJECTED_EVENT_SIZE_MAX];
	char **split;
	int split_count;
	struct kamino_event event;

	mutex_lock(&faker->lock);

	if (!faker->enabled) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret = simple_write_to_buffer(mem, INJECTED_EVENT_SIZE_MAX,
				ppos, user_buf, count);
	if (ret < 0)
		goto out;


	strreplace(mem, ',', ' ');
	split = argv_split(GFP_KERNEL, mem, &split_count);

	if (split_count != INJECTED_EVENT_NUM_ARGS) {
		ret = -EINVAL;
		goto out;
	}

	/* Event status */
	switch(split[0][0]) {
	case 'i':
		event.status = KAMINO_FAULT_INVALID_CHG;
		break;
	case 'm':
		event.status = KAMINO_FAULT_MOISTURE;
		break;
	case 'f':
		event.status = KAMINO_CHG_FAST;
		break;
	case 's':
		event.status = KAMINO_CHG_SLOW;
		break;
	default:
		ret = -EINVAL;
		goto out_with_argv;
	}

	ret = kstrtobool(&split[1][0], &event.in_reauth);
	if (ret != 0)
		goto out_with_argv;

	event.time = google_kamino_gettime();
	event.faked = true;

	google_kamino_on_new_event(kamino, &event);

	ret = count;

out_with_argv:
	argv_free(split);
out:
	mutex_unlock(&faker->lock);
	return ret;
}
static const struct file_operations inject_event_fops = {
	.open	= simple_open,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	.llseek	= no_llseek,
#endif
	.read	= NULL,
	.write	= inject_event_write,
};

void google_kamino_event_faker_setup_debugfs(struct kamino *kamino) {
	struct kamino_event_faker *faker = &kamino->event_faker;

	if (!kamino->debugfs_dir)
		return;

	faker->debugfs_dir =
		debugfs_create_dir("event_faker", kamino->debugfs_dir);

	if (IS_ERR_OR_NULL(faker->debugfs_dir)) {
		faker->debugfs_dir = NULL;
		return;
	}

	debugfs_create_file("enabled", 0644, faker->debugfs_dir,
			kamino, &enabled_fops);
	debugfs_create_file("inject_event", 0644, faker->debugfs_dir,
			kamino, &inject_event_fops);
}
#endif
