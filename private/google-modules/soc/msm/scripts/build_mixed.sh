#!/bin/bash -e
# SPDX-License-Identifier: GPL-2.0
#
# Generate GKI kernel and device kernel binaries
trap 'echo $0: line $LINENO: command exited with status $? >&2' ERR

source private/google-modules/soc/msm/scripts/script_utils.sh

function check_dirty_common() {
  local device_name=$(echo ${DEVICE_BAZEL_DIST_TARGET} | cut -d '/' -f6 | cut -d ':' -f1)

  if [[ -f ${GKI_ARTIFACTS_DIR}/vmlinux ]]; then
    local sha_file=vmlinux
  else
    local sha_file=boot.img
  fi

  local prebuilts_sha=$(strings ${GKI_ARTIFACTS_DIR}/${sha_file} |
                        egrep "Linux version [0-9]+\.[0-9]+" |
                        sed -n "s/^.*-g\([0-9a-f]\{12\}\)-.*/\1/p;q")
  local common_sha=$(git -C common/ log -1 --abbrev=12 --pretty="format:%h")
  local modified_kernel_files=$(git -C common/ --no-optional-locks status -uno --porcelain ||
                                git -C common/ diff-index --name-only HEAD)
  if [[ "${prebuilts_sha}" != "${common_sha}" ]] || [[ -n "${modified_kernel_files}" ]]; then
    echo "WARNING: There are changes in common/ which are not in the prebuilts."
    echo "Because you did not specify --config=build_gki, $0 defaulted to building"
    echo "with the prebuilts. Please be aware that your changes to common/ will not"
    echo "be present in the final images. If you have made changes to common/"
    echo "that you wish to build, it is recommended you invoke the build script"
    echo "with --config=build_gki. Otherwise, the prebuilts will be used."
  fi
}

function emit_gki_prebuilts_id() {
  sed -n 's/.*ab\([0-9]\+\).*/\1/p;q' $PREBUILTS_DIR/gki-info.txt
}

function log_build_information() {
  # Bazel gives preference to command line arguments
  if [[ $CMDLINE_BUILD_GKI_KERNEL -eq 1 ]]; then
    echo "BUILD INFO: Core-kernel generated from common/ source tree since --config=build_gki was detected on the command line"
  elif [[ $CMDLINE_NO_SYMBOL_TRIMMING -eq 1 ]]; then
    echo "BUILD INFO: Core-kernel generated from common/ source tree and symbol trimming disabled since --config=disable_symbol_trimming was detected on the command line"
  elif [[ $CMDLINE_USE_GKI_PREBUILTS -eq 1 ]]; then
    local gki_build_id=$(emit_gki_prebuilts_id)
    echo "BUILD INFO: Built device-kernel artifacts against GKI prebuilts from ab/$gki_build_id - kernel_aarch64 since --config=use_prebuilt_gki was detected on the command line"
    check_dirty_common
  elif [[ $BAZELRC_BUILD_GKI_KERNEL -eq 1 ]]; then
    echo "BUILD INFO: Core-kernel generated from common/ source tree since --config=build_gki was detected in device.bazelrc"
  elif [[ $BAZELRC_NO_SYMBOL_TRIMMING -eq 1 ]]; then
    echo "BUILD INFO: Core-kernel generated from common/ source tree with symbol trimming disabled since --config=disable_symbol_trimming was detected in device.bazelrc"
  elif [[ $BAZELRC_USE_GKI_PREBUITLS -eq 1 ]]; then
    local gki_build_id=$(emit_gki_prebuilts_id)
    echo "BUILD INFO: Built device-kernel artifacts against GKI prebuilts from ab/$gki_build_id - kernel_aarch64 since --config=use_prebuilt_gki was detected in device.bazelrc"
    check_dirty_common
  fi
}

function validate_build_environment() {
  # Catch and flag deprecated argument usage
  if [[ "${BUILD_KERNEL}" == "1" ]]; then
    echo "Setting BUILD_KERNEL=1 from the command line has been deprecated." >&2
    echo "Please invoke the build script with --config=build_gki instead." >&2
    exit 1
  fi

  if [[ "${DISABLE_SYMBOL_TRIMMING}" == "1" ]]; then
    echo "Setting DISABLE_SYMBOL_TRIMMING=1 from the command line has been deprecated." >&2
    echo "Please invoke the build script with --config=disable_symbol_trimming instead." >&2
    exit 1
  fi

  if [[ -n "${MCU_TRANSPORT_AUTOBAHN}" ]] || \
     [[ -n "${MCU_TRANSPORT_NANOHUB}" ]]; then
    echo "Setting MCU_TRANSPORT_AUTOBAHN=1 and MCU_TRANSPORT_NANOHUB=1 from the command line has been deprecated." >&2
    echo "Please invoke the build script with --config=mcu_transport_autobahn, " \
	                                     "--config=mcu_transport_autobahn_frame_1032b or " \
					     "--config=mcu_transport_nanohub instead." >&2
    exit 1
  fi

  # device.bazelrc dictates the default behavior, so ensure that it is
  # configured with at most one of the following build parameters:
  #
  # --config=build_gki
  # --config=use_prebuilt_gki
  # --config=disable_symbol_trimming (implies --config=build_gki)
  local nr_bazelrc_build_configs=$((BAZELRC_BUILD_GKI_KERNEL + BAZELRC_USE_GKI_PREBUITLS +
                                    BAZELRC_NO_SYMBOL_TRIMMING))

  if [[ $nr_bazelrc_build_configs -ne 1 ]]; then
    echo "Please set exactly one of the following parameters in device.bazelrc" >&2
    echo "build --config=build_gki" >&2
    echo "build --config=use_prebuilt_gki" >&2
    echo "build --config=disable_symbol_trimming" >&2
    exit 1
  fi

  # Similarly, if one of the config options is specified in the command line,
  # only one of them should be.
  local nr_cmdline_build_configs=$((CMDLINE_BUILD_GKI_KERNEL + CMDLINE_USE_GKI_PREBUILTS +
                                    CMDLINE_NO_SYMBOL_TRIMMING))

  if [[ $nr_cmdline_build_configs -gt 1 ]]; then
    echo "Only one of --config=build_gki --config=disable_symbol_trimming --config=use_prebuilt_gki" >&2
    echo "may be passed through the command line." >&2
    exit 1
  fi
}

function shell_var_grep() {
  local var_name="$1"
  local match="$2"

  if [[ ${!var_name} =~ (^|[[:space:]])$2($|[[:space:]]) ]]; then
    echo 1
  else
    echo 0
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dist_target)
      DEVICE_BAZEL_DIST_TARGET="$2"
      shift 2
      ;;
    *)
      BAZEL_BUILD_PARAMETERS="${BAZEL_BUILD_PARAMETERS} $1"
      shift
      ;;
  esac
done

if [[ -z "${DEVICE_BAZEL_DIST_TARGET}" ]]; then
  echo "--dist_target must all be specified to create a mixed bazel build." >&2
  exit 1
fi

if ! [[ -f "${BASE_BAZELRC_FILE}" ]]; then
  echo "${BASE_BAZELRC_FILE} must exist to create a mixed bazel build." >&2
  exit 1
fi

# Use numbers here to make validating how many of these arguments are set easy.
BAZELRC_BUILD_GKI_KERNEL=`grep -c "^build --config=build_gki" $BASE_BAZELRC_FILE || true`
BAZELRC_NO_SYMBOL_TRIMMING=`grep -c "^build --config=disable_symbol_trimming" $BASE_BAZELRC_FILE || true`
BAZELRC_USE_GKI_PREBUITLS=`grep -c "^build --config=use_prebuilt_gki" $BASE_BAZELRC_FILE || true`

CMDLINE_BUILD_GKI_KERNEL=$(shell_var_grep BAZEL_BUILD_PARAMETERS "--config=build_gki")
CMDLINE_NO_SYMBOL_TRIMMING=$(shell_var_grep BAZEL_BUILD_PARAMETERS "--config=disable_symbol_trimming")
CMDLINE_USE_GKI_PREBUILTS=$(shell_var_grep BAZEL_BUILD_PARAMETERS "--config=use_prebuilt_gki")

validate_build_environment

BAZEL_BUILD_PARAMETERS="--config=fast --config=stamp ${BAZEL_BUILD_PARAMETERS}"

tools/bazel run ${BAZEL_BUILD_PARAMETERS} ${DEVICE_BAZEL_DIST_TARGET}

log_build_information
