#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Generate GKI kernel and P11-specific kernel binaries

private/google-modules/soc/msm/scripts/build_mixed.sh \
  --dist_target //private/devices/google/p11:sw5100_p11_dist \
  --config=p11 "$@"
