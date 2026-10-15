/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Google LLC */

#ifndef __GOOGLE_KAMINO_PERIODIC_REAUTH_H__
#define __GOOGLE_KAMINO_PERIODIC_REAUTH_H__

#include "google-kamino.h"

int google_kamino_periodic_reauth_init(struct kamino *kamino);
void google_kamino_periodic_reauth_deinit(struct kamino *kamino);
void google_kamino_periodic_reauth_psy_changed(struct kamino *kamino);
void google_kamino_periodic_reauth_setup_debugfs(struct kamino *kamino);
void google_kamino_periodic_reauth_pm_suspend(struct kamino *kamino);
void google_kamino_periodic_reauth_set_psy_faking_enabled(struct kamino *kamino,
							bool faking_enabled);

static inline bool google_kamino_periodic_reauth_in_progress(struct kamino *kamino)
{
	return READ_ONCE(kamino->periodic_reauth.in_progress);
}

#endif /* __GOOGLE_KAMINO_PERIODIC_REAUTH_H__ */
