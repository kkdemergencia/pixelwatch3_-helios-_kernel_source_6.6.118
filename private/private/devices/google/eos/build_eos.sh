#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Generate GKI kernel and Eos-specific kernel binaries

private/google-modules/soc/msm/scripts/build_mixed.sh \
  --dist_target //private/devices/google/eos:sw5100_eos_dist \
  --config=eos "$@"
