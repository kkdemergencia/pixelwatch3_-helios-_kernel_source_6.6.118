#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Generate GKI kernel and Seluna-specific kernel binaries

private/google-modules/soc/msm/scripts/build_mixed.sh \
  --dist_target //private/devices/google/seluna:sw5100_seluna_dist \
  --config=seluna "$@"
