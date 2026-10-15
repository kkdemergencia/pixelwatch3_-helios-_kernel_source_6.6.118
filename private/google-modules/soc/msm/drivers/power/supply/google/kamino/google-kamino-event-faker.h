/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Google LLC */

#ifndef __GOOGLE_KAMINO_EVENT_FAKER_H__
#define __GOOGLE_KAMINO_EVENT_FAKER_H__

#include "google-kamino.h"

void google_kamino_event_faker_init(struct kamino *kamino);
void google_kamino_event_faker_deinit(struct kamino *kamino);
void google_kamino_event_faker_setup_debugfs(struct kamino *kamino);

bool google_kamino_event_faker_enabled(struct kamino *kamino);

#endif /* __GOOGLE_KAMINO_EVENT_FAKER_H__ */