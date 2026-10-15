/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Google LLC */


#ifndef __GOOGLE_KAMINO_MOISTURE_DETECT_MITIGATION_H__
#define __GOOGLE_KAMINO_MOISTURE_DETECT_MITIGATION_H__

#include "google-kamino.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

int google_kamino_moisture_detect_mitigation_init(struct kamino *kamino);
void google_kamino_moisture_detect_mitigation_deinit(struct kamino *kamino);

void
google_kamino_moisture_detect_mitigation_on_new_status(struct kamino *kamino,
						const enum kamino_status status,
						const bool in_reauth);

int
google_kamino_moisture_detect_mitigation_override(struct kamino *kamino,
						struct kamino_event *override_out);

void google_kamino_moisture_detect_mitigation_psy_changed(struct kamino *kamino);

void google_kamino_moisture_detect_mitigation_setup_sysfs(struct kamino *kamino);
void google_kamino_moisture_detect_mitigation_teardown_sysfs(struct kamino *kamino);

#ifdef CONFIG_DEBUG_FS
void google_kamino_moisture_detect_mitigation_setup_debugfs(struct kamino *kamino);
#endif

#endif /* __GOOGLE_KAMINO_MOISTURE_DETECT_MITIGATION_H__ */