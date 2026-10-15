/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Google LLC */

#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/of.h>
#include "google-kamino.h"
#include "google-kamino-extcon.h"
#include "google-kamino-periodic-reauth.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#define EXTCON_ID EXTCON_DOCK

static const int supported_cables[] = {
	EXTCON_ID,
	EXTCON_NONE, /* sentinel */
};

void google_kamino_extcon_update(struct kamino *kamino) {
	struct kamino_extcon *extcon = &kamino->extcon;
	enum kamino_status status;
	bool connected;
	int ret = 0;

	mutex_lock(&extcon->lock);

	if (!extcon->dev)
		goto out;

	status = smp_load_acquire(&kamino->active_status);
	connected = google_kamino_psy_prop_present(kamino)
		&& google_kamino_is_chg_status(status);

	if (extcon->spoof) {
		connected = extcon->spoof_val;
	}

	if (connected == extcon_get_state(extcon->dev, EXTCON_ID))
		goto out;

	dev_info(kamino->dev, "Updating extcon state to: %s\n",
		connected ? "connected" : "disconnected");

	ret = extcon_set_state_sync(extcon->dev, EXTCON_ID, connected);

out:
	mutex_unlock(&extcon->lock);

	if (ret != 0)
		dev_warn(kamino->dev, "Could not update extcon: %d\n", ret);
}

void google_kamino_extcon_kamino_active_status_changed(struct kamino *kamino) {
	google_kamino_extcon_update(kamino);
}

void google_kamino_extcon_psy_changed(struct kamino *kamino) {
	google_kamino_extcon_update(kamino);
}

int google_kamino_extcon_init(struct kamino *kamino) {
	int ret;

	mutex_init(&kamino->extcon.lock);
	if (!of_property_read_bool(kamino->dev->of_node,
				"google,provide-extcon")) {
		dev_warn(kamino->dev, "Not providing extcon!\n");
		return -ENOTSUPP;
	}

	kamino->extcon.dev = devm_extcon_dev_allocate(kamino->dev, supported_cables);

	if (IS_ERR(kamino->extcon.dev)) {
		ret = PTR_ERR(kamino->extcon.dev);
		dev_err(kamino->dev,
			"Failed to allocate extcon device: %d\n", ret);
		return ret;
	}

	ret = devm_extcon_dev_register(kamino->dev, kamino->extcon.dev);
	if (ret < 0) {
		dev_err(kamino->dev,
			"Failed to register extcon device: %d\n", ret);
	}

	return ret;
}

void google_kamino_extcon_deinit(struct kamino *kamino) {
	mutex_destroy(&kamino->extcon.lock);
}

#ifdef CONFIG_DEBUG_FS

static int spoof_enabled_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_extcon *extcon = &kamino->extcon;

	mutex_lock(&extcon->lock);
	*val = extcon->spoof;
	mutex_unlock(&extcon->lock);

	return 0;
}

static int spoof_enabled_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_extcon *extcon = &kamino->extcon;

	mutex_lock(&extcon->lock);
	extcon->spoof = (val != 0);
	mutex_unlock(&extcon->lock);
	google_kamino_extcon_update(kamino);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(spoof_enabled_fops, spoof_enabled_get,
			spoof_enabled_set, "%llu\n");

static int spoof_val_get(void *data, u64 *val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_extcon *extcon = &kamino->extcon;

	mutex_lock(&extcon->lock);
	*val = extcon->spoof_val;
	mutex_unlock(&extcon->lock);

	return 0;
}

static int spoof_val_set(void *data, u64 val)
{
	struct kamino *kamino = (struct kamino *)data;
	struct kamino_extcon *extcon = &kamino->extcon;

	mutex_lock(&extcon->lock);
	extcon->spoof_val = (val != 0);
	mutex_unlock(&extcon->lock);
	google_kamino_extcon_update(kamino);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(spoof_val_fops, spoof_val_get, spoof_val_set, "%llu\n");

void google_kamino_extcon_setup_debugfs(struct kamino *kamino) {
	struct kamino_extcon *extcon = &kamino->extcon;

	if (!kamino->debugfs_dir)
		return;

	extcon->debugfs_dir =
		debugfs_create_dir("extcon_spoof", kamino->debugfs_dir);

	if (IS_ERR_OR_NULL(extcon->debugfs_dir)) {
		extcon->debugfs_dir = NULL;
		return;
	}

	debugfs_create_file("enabled", 0644, extcon->debugfs_dir,
			kamino, &spoof_enabled_fops);
	debugfs_create_file("val", 0644, extcon->debugfs_dir,
			kamino, &spoof_val_fops);
}
#endif
