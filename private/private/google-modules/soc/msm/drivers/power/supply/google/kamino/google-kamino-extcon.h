/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Google LLC */

#ifndef __GOOGLE_KAMINO_EXTCON_H__
#define __GOOGLE_KAMINO_EXTCON_H__

#include "google-kamino.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

int google_kamino_extcon_init(struct kamino *kamino);
void google_kamino_extcon_deinit(struct kamino *kamino);
void google_kamino_extcon_kamino_active_status_changed(struct kamino *kamino);
void google_kamino_extcon_psy_changed(struct kamino *kamino);

#ifdef CONFIG_DEBUG_FS
void google_kamino_extcon_setup_debugfs(struct kamino *kamino);
#endif

#endif /* __GOOGLE_KAMINO_EXTCON_H__ */
