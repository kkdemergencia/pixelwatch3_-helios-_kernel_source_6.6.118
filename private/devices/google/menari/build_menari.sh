#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Generate GKI kernel and Menari-specific kernel binaries

private/google-modules/soc/msm/scripts/build_mixed.sh \
  --dist_target //private/devices/google/menari:sw5100_menari_dist \
  --config=menari "$@"
