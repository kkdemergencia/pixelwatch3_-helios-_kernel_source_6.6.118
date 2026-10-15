#!/bin/bash -e
# SPDX-License-Identifier: GPL-2.0
#
# Update the common kernel git sha, merge new changes from the common kernel
# to the device kernel, and update the GKI kernel prebuilt binaries.

set -o pipefail

trap 'echo $0: line $LINENO: command exited with status $? >&2; rm -rf $DDK_WORKSPACE_TMPDIR' ERR

source private/google-modules/soc/msm/scripts/script_utils.sh

# Tracking bug of aosp-merge event
MERGE_BUG="NA"

# GKI boot image build-id
BOOT_IMAGE_BUILDID=""

# GKI Kernel Branch (on aosp gerrit)
AOSP_BRANCH=""

# Whether or not to push commits to gerrit
PUSH=0

CUR_DIR=$(pwd)

KERNEL_VERSION=""
KERNEL_PATCHLEVEL=""
KERNEL_SUBLEVEL=""
AOSP_COMMIT_INFO=""

readonly MANIFEST_DIR=".repo/manifests"
readonly GKI_KERNEL_DIR="common"
readonly DEVICE_KERNEL_DIR="private/msm-google"
readonly DOWNLOAD_GKI_BINARIES_SCRIPT_DIR="external/kleaf/build/bootstrap"

#-----------------------------------------------------------------------
# Reset color
Color_Off='\033[0m'       # Text Reset
# Regular Colors
Green='\033[0;32m'        # Green
Yellow='\033[0;33m'       # Yellow

function usage() {
  cat <<- EOF
    Usage:

    Required arguments:

    aosp_branch:            The GKI branch that was used to generate the boot image.
    boot_image_build_id:    The build ID of the prebuilt boot image to download.

    Optional arguments:

    merge_bug:              The bug number associated with this merge.
    push:                   Whether or not to automatically push generated commits
                            to gerrit for review. This is set to 0 by default.

    Sample invocation:

    $0 --merge_bug 261218518 --boot_image_build_id 9611439 \\
      --aosp_branch android15-6.6-2024-07

    Note:
      1. You can use --push when invoking $0 to automatically push the generated commits
      to gerrit for review.
EOF
}

function sanitize_args() {
  if [[ -z "${BOOT_IMAGE_BUILDID}" ]]; then
    usage
    exit 1
  elif [[ ! -d "${CUR_DIR}/${GKI_KERNEL_DIR}" ]]; then
    echo "Cannot find GKI kernel directory ${GKI_KERNEL_DIR}/" >&2
    exit 1
  elif [[ ! -d "${CUR_DIR}/${DEVICE_KERNEL_DIR}" ]]; then
    echo "Cannot find device kernel directory ${DEVICE_KERNEL_DIR}/" >&2
    exit 1
  elif [[ ! -d "${CUR_DIR}/${PREBUILTS_DIR}" ]]; then
    echo "Cannot find prebuilts directory ${PREBUILTS_DIR}/" >&2
    exit 1
  elif [[ -z "${KERNEL_MANIFEST_BRANCH}" ]]; then
    echo "Unable to find manifest branch name" >&2
    exit 1
  elif [[ -z "${BRANCH_DEVICE_KERNEL}" ]]; then
    echo "Unable to find device kernel branch name" >&2
    exit 1
  elif [[ -z "${PREBUILTS_BRANCH}" ]]; then
    echo "Unable to find prebuilts branch name" >&2
    exit 1
  elif [[ -z "${AOSP_BRANCH}" ]]; then
    usage
    exit 1
  fi
}

function sync_kernel_repos() {
  local manifest_proj_url="persistent-https://partner-android.git.corp.google.com/kernel/manifest"

  echo -e "\n${Yellow}Running repo init and syncing ${KERNEL_MANIFEST_BRANCH}${Color_Off}"

  repo init -q -u "${manifest_proj_url}" -b "${KERNEL_MANIFEST_BRANCH}"

  repo sync -q -cd -j$(nproc)
}

function get_gki_kernel_info() {
  pushd ${CUR_DIR}/${GKI_KERNEL_DIR} > /dev/null

  KERNEL_VERSION=$(grep -r -m 1 "VERSION =" Makefile | sed 's/.*VERSION = //g')
  KERNEL_PATCHLEVEL=$(grep -r -m 1 "PATCHLEVEL =" Makefile | \
                      sed 's/.*PATCHLEVEL = //g')
  KERNEL_SUBLEVEL=$(grep -r -m 1 "SUBLEVEL =" Makefile | \
                    sed 's/.*SUBLEVEL = //g')
  AOSP_COMMIT_INFO=$(git log --pretty=format:'%h %s' -1)

  popd > /dev/null
}

function update_manifest_and_sync_gki_kernel() {
  local rev_current_aosp

  /google/data/ro/projects/android/fetch_artifact --bid ${BOOT_IMAGE_BUILDID} \
              --target kernel_aarch64 "manifest_${BOOT_IMAGE_BUILDID}.xml"

  REV_NEW_AOSP=$(grep -r "=\"kernel/common\""\
                 manifest_${BOOT_IMAGE_BUILDID}.xml | \
                 sed 's/.*revision="//g'| sed 's/".*//g')
  rm manifest_${BOOT_IMAGE_BUILDID}.xml

  rev_current_aosp=$(repo info kernel/${GKI_KERNEL_DIR} | grep "Current revision" | \
                     sed 's/Current revision: //g')

  pushd ${MANIFEST_DIR} > /dev/null

  sed -i "s/${rev_current_aosp}/${REV_NEW_AOSP}/" default.xml

  popd > /dev/null

  echo -e "\n${Yellow}Syncing the GKI kernel to new revision: ${REV_NEW_AOSP}${Color_Off}"
  repo sync -q -cd -j$(nproc)
}

function commit_and_push_manifest_update() {
  local merge_msg=$(printf "Update ${AOSP_BRANCH} sync point to (${KERNEL_VERSION}.${KERNEL_PATCHLEVEL}.${KERNEL_SUBLEVEL})\n\nSync SHA:\n${AOSP_COMMIT_INFO}\n\nBug: ${MERGE_BUG}")

  pushd ${MANIFEST_DIR} > /dev/null

  git add default.xml
  git commit --quiet -s -m "${merge_msg}"

  if [[ $PUSH -eq 1 ]]; then
    echo -e "\n${Yellow}Pushing manifest updates...${Color_Off}"
    git push origin HEAD:refs/for/${KERNEL_MANIFEST_BRANCH} -o topic=${TOPIC}
  fi

  popd > /dev/null
}

function update_gki_kernel() {
  get_gki_kernel_info
  echo -e "\n${Green}Current GKI Kernel Info${Color_Off}"
  echo -e "KERNEL_VERSION = [${Green}${KERNEL_VERSION}${Color_Off}]"
  echo -e "KERNEL_PATCHLEVEL = [${Green}${KERNEL_PATCHLEVEL}${Color_Off}]"
  echo -e "KERNEL_SUBLEVEL = [${Green}${KERNEL_SUBLEVEL}${Color_Off}]"
  echo -e "AOSP_COMMIT_INFO = [${Green}${AOSP_COMMIT_INFO}${Color_Off}]\n"

  update_manifest_and_sync_gki_kernel

  get_gki_kernel_info
  echo -e "\n${Green}New GKI Kernel Info:${Color_Off}"
  echo -e "KERNEL_VERSION = [${Green}${KERNEL_VERSION}${Color_Off}]"
  echo -e "KERNEL_PATCHLEVEL = [${Green}${KERNEL_PATCHLEVEL}${Color_Off}]"
  echo -e "KERNEL_SUBLEVEL = [${Green}${KERNEL_SUBLEVEL}${Color_Off}]"
  echo -e "AOSP_COMMIT_INFO = [${Green}${AOSP_COMMIT_INFO}${Color_Off}]"

  commit_and_push_manifest_update
}

function merge_aosp_to_device_kernel() {
  local aosp_remote
  local merge_msg
  local bugid_script_dir
  local merge_commit_sha

  echo -e "\n${Yellow}Merging ${GKI_KERNEL_DIR} SHA ${REV_NEW_AOSP} to ${DEVICE_KERNEL_DIR} ...${Color_Off}"

  bugid_script_dir=${CUR_DIR}/${SCRIPTS_DIR}

  pushd ${CUR_DIR}/${DEVICE_KERNEL_DIR} > /dev/null

  aosp_remote=$(git remote -v | grep -o -m 1 -e "^aosp.*common" || true)
  if [[ -z "${aosp_remote}" ]]; then
    aosp_remote=$(git -C ${CUR_DIR}/${GKI_KERNEL_DIR} remote -v | grep -o -m 1 -e "^aosp.*common")
    git remote add ${aosp_remote}
  fi

  git fetch --quiet aosp "${AOSP_BRANCH}"
  merge_msg=$(printf "Merge ${AOSP_BRANCH} into ${BRANCH_DEVICE_KERNEL}\n\nMerge SHA:\n${AOSP_COMMIT_INFO}\n\nBug: ${MERGE_BUG}")
  git merge --quiet --signoff --no-ff -X patience ${REV_NEW_AOSP} \
            -m "${merge_msg}" || true

  # Merge conflicts detected
  if ! git diff --quiet --diff-filter=U; then
    git add -A
    # Commit first since BugId.py expects the merge commit to exist before
    # running.
    git commit -a --no-edit --quiet --no-verify -s
    echo -e "\n${Yellow}WARNING: Merge conflicts detected, please resolve them.${Color_Off}"
  fi

  merge_commit_sha=$(git rev-list -n 1 HEAD ^partner/${BRANCH_DEVICE_KERNEL})
  if [[ -n "${merge_commit_sha}" ]]; then
    echo "${merge_msg}" >> CommitInfo.txt
    python3 ${bugid_script_dir}/BugId.py CommitInfo.txt
    git commit --amend --quiet -s -F CommitInfo.txt
    rm CommitInfo.txt

    if [[ $PUSH -eq 1 ]]; then
      echo -e "\n${Yellow}Pushing updates to ${DEVICE_KERNEL_DIR}${Color_Off}"
      git push partner HEAD:refs/for/${BRANCH_DEVICE_KERNEL} -o topic=${TOPIC}
    fi
  # The changes were already part of this branch already (e.g. they
  # were cherry-picked earlier).
  else
    echo -e "\n${Yellow}No updates required to ${DEVICE_KERNEL_DIR}${Color_Off}"
  fi

  popd > /dev/null
}

function update_gki_prebuilts() {
  local current_build_id
  local merge_msg=$(printf "Update the GKI binaries to ab/${BOOT_IMAGE_BUILDID}\n\nUse GKI prebuilt binaries from ab/${BOOT_IMAGE_BUILDID}\n\nBug: ${MERGE_BUG}")
  local dl_gki_bins_script_dir_path=${CUR_DIR}/${DOWNLOAD_GKI_BINARIES_SCRIPT_DIR}

  echo -e "\n${Yellow}Updating GKI prebuilt binaries to ab/${BOOT_IMAGE_BUILDID} ...${Color_Off}"

  pushd ${CUR_DIR}/${PREBUILTS_DIR} > /dev/null

  DDK_WORKSPACE_TMPDIR=$(mktemp -d)

  # Use --ddk_workspace $DDK_WORKSPACE_TMPDIR, so that the script doesn't modify the actual
  # MODULE.bazel file at the root of the repo, since it doesn't need
  # modifications.
  python3 ${dl_gki_bins_script_dir_path}/init.py --prebuilts_dir $DDK_WORKSPACE_TMPDIR --build_id ${BOOT_IMAGE_BUILDID} --ddk_workspace $DDK_WORKSPACE_TMPDIR

  rm -rf *
  cp -rv $DDK_WORKSPACE_TMPDIR/* .
  # MODULE.bazel is not needed and we don't need $DDK_WORKSPACE_TMPDIR anymore.
  rm -rf MODULE.bazel $DDK_WORKSPACE_TMPDIR
  git add --all
  git commit --quiet -s -m "${merge_msg}"

  if [[ $PUSH -eq 1 ]]; then
    echo -e "\n${Yellow}Pushing updates to GKI prebuilts${Color_Off}"
    git push partner HEAD:refs/for/${PREBUILTS_BRANCH} -o topic=${TOPIC}
  fi

  popd > /dev/null
}

function log_push_disabled_notice() {
  cat <<- EOF
    Commits have been generated for integrating ACK SHA ${REV_NEW_AOSP}
    but have not been pushed to gerrit.

    To ensure that the commits are pushed to gerrit, rerun the script with
    --push or push each commit to gerrit manually with the following
    commands:

    # Manifest update
    cd ${MANIFEST_DIR}
    git push origin HEAD:refs/for/${KERNEL_MANIFEST_BRANCH}
    cd ..

    # Device kernel update
    cd ${DEVICE_KERNEL_DIR}
    git push partner HEAD:refs/for/${BRANCH_DEVICE_KERNEL}
    cd ..

    # GKI prebuilts update
    cd ${PREBUILTS_DIR}
    git push partner HEAD:refs/for/${PREBUILTS_BRANCH}
    cd ..
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --boot_image_build_id)
      BOOT_IMAGE_BUILDID="$2"
      if [[ ! "${BOOT_IMAGE_BUILDID}" =~ ^[0-9]+$ ]]; then
        usage
	exit 1
      fi
      shift 2
      ;;
    --merge_bug)
      MERGE_BUG="$2"
      if [[ ! "${MERGE_BUG}" =~ ^[0-9]+$ ]]; then
        usage
	exit 1
      fi
      shift 2
      ;;
    --aosp_branch)
      AOSP_BRANCH="$2"
      if [[ -z "${AOSP_BRANCH}" ]] || [[ "${AOSP_BRANCH}" =~ ^-+ ]]; then
        usage
	exit 1
      fi
      shift 2
      ;;
    --push)
      PUSH=1
      shift
      ;;
    *)
      usage
      exit 1
      ;;
  esac
done

KERNEL_MANIFEST_BRANCH=$(repo info | sed -n 's/Manifest branch: \(.*\)/\1/p')
BRANCH_DEVICE_KERNEL=$(repo info ${DEVICE_KERNEL_DIR} | sed -n 's/Manifest revision: \(.*\)/\1/p')
PREBUILTS_BRANCH=$(repo info ${PREBUILTS_DIR} | sed -n 's/Manifest revision: \(.*\)/\1/p')

sanitize_args

# Gerrit Topic e.g.:
# WK10_AOSP_MERGE_android15-msm-pixelwatch-6.6_AND_android15-6.6
TOPIC=${TOPIC:-WK`date +%U`_AOSP_MERGE_${KERNEL_MANIFEST_BRANCH}_AND_${AOSP_BRANCH}}

echo -e "\nBegin.\n"
echo -e "TOPIC = [${Green}${TOPIC}${Color_Off}]"

sync_kernel_repos

update_gki_kernel

merge_aosp_to_device_kernel

update_gki_prebuilts

echo -e "\nDone.\n"

if [[ $PUSH -ne 1 ]]; then
  log_push_disabled_notice
fi
