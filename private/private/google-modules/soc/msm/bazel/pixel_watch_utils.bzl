# SPDX-License-Identifier: GPL-2.0-or-later

"""Utility file for devices that are based on the sw5100 SoC."""

load(
    "@bazel_skylib//lib:paths.bzl",
    "paths",
)
load(
    "@bazel_skylib//rules:write_file.bzl",
    "write_file",
)
load(
    "@kleaf//build/kernel/kleaf:kernel.bzl",
    "kernel_build",
    "kernel_images",
    "kernel_modules_install",
)
load(
    "@rules_pkg//pkg:install.bzl",
    "pkg_install",
)
load(
    "@rules_pkg//pkg:mappings.bzl",
    "pkg_files",
)
load(
    ":bazel/constants.scl",
    "KERNEL_DIR",
)
load(
    ":bazel/merged_files_and_string_list.bzl",
    "merged_files_and_string_list",
)
load(
    ":bazel/modules.bzl",
    "SW5100_VENDOR_DLKM_BLOCKLIST_MODULES",
    "SW5100_VENDOR_DLKM_MODULES",
    "SW5100_VENDOR_KERNEL_BOOT_CHARGER_MODULES",
    "SW5100_VENDOR_KERNEL_BOOT_MODULES",
    "SW5100_VENDOR_KERNEL_BOOT_RECOVERY_MODULES",
    "get_sw5100_in_tree_modules",
)

def _format_modules_blocklist_to_string_list(modules_blocklist):
    output_list = []

    for module in modules_blocklist:
        module_name = paths.basename(module.removesuffix(".ko"))
        output_list.append("blocklist {}".format(module_name))

    return output_list

def sw5100_pixel_watch_dtbo_image(
        name,
        dtstree,
        dtb_overlays):
    """Generates the rules for a kernel_images() target that builds a dtbo.img

    This macro can be used for generating the dtbo.img for a specific device.
    It defines kernel_build(), kernel_modules_install(), and kernel_images()
    targets which are all used to form a dtbo.img.

    Args:
        name: The name of the device.

        dtstree: A label referring to a kernel_dtstree() target.

        dtb_overlays: A list containing the names of the overlays.
    """
    kernel_build(
        name = "{}_overlays".format(name),
        build_config = Label(":build.config.sw5100.generated"),
        dtstree = dtstree,
        make_goals = [
            "dtbs",
        ],
        outs = dtb_overlays,
        srcs = [
            "//{}:kernel_aarch64_sources".format(KERNEL_DIR),
        ],
    )

    kernel_modules_install(
        name = "empty_modules_install",
    )

    kernel_images(
        name = name,
        build_dtbo = True,
        dtbo_srcs = [":{}_overlays/".format(name) + overlay for overlay in dtb_overlays],
        kernel_build = ":{}_overlays".format(name),
        kernel_modules_install = ":empty_modules_install",
    )

def sw5100_pixel_watch_dist(
        name,
        device_name,
        device_vendor_dlkm_blocklist,
        device_vendor_dlkm_modules,
        device_vendor_kernel_boot_modules,
        device_vendor_kernel_boot_recovery_modules,
        dtbo_image):
    """Generates the rules to build kernel artifacts for sw5100 based devices.

    This macro generates the rules to compile the kernel, kernel modules,
    devicetree blob, and kernel images (e.g. boot.img, vendor_kernel_boot.img,
    etc). The dtbo.img label is provided by the caller and added to the
    distribution targets.

    The macro also generates the rules to merge the UAPI headers from the GKI
    kernel under common/, the private/msm-google tree and the kernel modules,
    and outputs them as a compressed tarball.

    Finally, the macro also generates the rules to collect all of the
    unstripped kernel modules for debugging purposes in a compressed
    tarball.

    Args:
        name: The name of the distribution target.

        device_name: The name of the device.

        device_vendor_dlkm_blocklist: A list containing the modules
            that should be blocked from being loaded automatically during second
            stage init.

        device_vendor_dlkm_modules: A list containing the device-specific modules
            that should be part of the vendor_dlkm.img. This list is joined with:

            All in-tree modules
            First stage init modules

            All modules not loaded in first stage init that need to be loaded
            after that should be part of this list.

        device_vendor_kernel_boot_modules: A list of device-specific kernel modules that
            should be loaded during first stage init. This list is concatenated with the
            following:

            `vendor_kernel_boot_modules.monaco` (file)
            `SW5100_VENDOR_KERNEL_BOOT_MODULES` (list)

        device_vendor_kernel_boot_recovery_modules: A list of device-specific kernel modules
            that should be loaded during first stage init when booting into recovery mode.
            This list is concatenated with the following:

            `vendor_kernel_boot_modules.monaco` (file)
            `SW5100_VENDOR_KERNEL_BOOT_MODULES` (list)
            `device_vendor_kernel_boot_modules` (list)
            `SW5100_VENDOR_KERNEL_BOOT_RECOVERY_MODULES` (list)

        dtbo_image: A label to a kernel_images() target used for building the
            dtbo.img for the device.
    """

    merged_files_and_string_list(
        name = "{}_vendor_kernel_boot_modules.cat".format(name),
        merge_files = [
            Label(":vendor_kernel_boot_modules.monaco"),
        ],
        extra_lines = SW5100_VENDOR_KERNEL_BOOT_MODULES + device_vendor_kernel_boot_modules,
    )

    merged_files_and_string_list(
        name = "{}_vendor_kernel_boot_charger_modules.cat".format(name),
        merge_files = [
            Label(":vendor_kernel_boot_modules.monaco"),
            ":{}_vendor_kernel_boot_modules.cat".format(name),
        ],
        extra_lines = SW5100_VENDOR_KERNEL_BOOT_CHARGER_MODULES,
    )

    merged_files_and_string_list(
        name = "{}_vendor_kernel_boot_recovery_modules.cat".format(name),
        merge_files = [
            Label(":vendor_kernel_boot_modules.monaco"),
            ":{}_vendor_kernel_boot_modules.cat".format(name),
        ],
        extra_lines = SW5100_VENDOR_KERNEL_BOOT_RECOVERY_MODULES +
                      device_vendor_kernel_boot_recovery_modules,
    )

    write_file(
        name = "{}_vendor_dlkm_modules_list".format(name),
        out = "vendor_dlkm_modules.sw5100",
        content = ["^kernel/{}".format(module) for module in get_sw5100_in_tree_modules()] +
                  SW5100_VENDOR_KERNEL_BOOT_MODULES + device_vendor_kernel_boot_modules +
                  SW5100_VENDOR_DLKM_MODULES + device_vendor_dlkm_modules,
    )

    merged_files_and_string_list(
        name = "{}_vendor_dlkm_blocklist.cat".format(name),
        merge_files = [
            Label(":vendor_dlkm.blocklist.monaco"),
        ],
        extra_lines = _format_modules_blocklist_to_string_list(SW5100_VENDOR_DLKM_BLOCKLIST_MODULES +
                                                               device_vendor_dlkm_blocklist),
    )

    kernel_images(
        name = "{}_images".format(name),
        boot_image_outs = [
            "ramdisk.lz4",
            "vendor_kernel_boot.img",
        ],
        build_boot = False,
        build_initramfs = True,
        build_vendor_dlkm = True,
        build_vendor_kernel_boot = True,
        deps = [
            Label("selinux/file_contexts"),
        ],
        kernel_build = Label(":sw5100"),
        kernel_modules_install = Label(":sw5100_modules_install"),
        modules_list = ":{}_vendor_kernel_boot_modules.cat".format(name),
        modules_charger_list = ":{}_vendor_kernel_boot_charger_modules.cat".format(name),
        modules_recovery_list = ":{}_vendor_kernel_boot_recovery_modules.cat".format(name),
        vendor_dlkm_modules_blocklist = ":{}_vendor_dlkm_blocklist.cat".format(name),
        vendor_dlkm_modules_list = ":{}_vendor_dlkm_modules_list".format(name),
        vendor_dlkm_props = Label("vendor_dlkm.props.sw5100"),
    )

    pkg_files(
        name = "{}_files".format(name),
        srcs = [
            ":{}_images".format(name),
            dtbo_image,
        ],
        prefix = device_name,
    )

    pkg_install(
        name = name,
        srcs = [
            Label(":sw5100_pkg_files"),
            ":{}_files".format(name),
        ],
        destdir = "out/sw5100",
    )
