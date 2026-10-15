# SPDX-License-Identifier: GPL-2.0-or-later

"""File containing information about kernel modules for sw5100 based devices."""

load(
    "//private/msm-google:monaco.bzl",
    "monaco_in_tree_modules",
)

_SW5100_ADDITIONAL_IN_TREE_MODULES = [
    # keep sorted
    "drivers/base/regmap/qti-regmap-debugfs.ko",
    "drivers/i2c/i2c-dev.ko",
    "drivers/watchdog/softdog.ko",
]

# These modules are not required on sw5100 devices and
# are disabled by the arch/arm64/configs/sw5100.fragment.
_SW5100_MODULES_EXCLUDE_LIST = [
    # keep sorted
    "drivers/phy/qualcomm/phy-qcom-ufs.ko",
    "drivers/power/supply/qcom/qpnp-smb5-main.ko",
    "drivers/power/supply/qcom/qpnp-smblite-main.ko",
    "drivers/power/supply/qcom/qti-qbg-main.ko",
    "drivers/remoteproc/qcom_rproc_slate.ko",
    "drivers/rpmsg/qcom_glink_slatecom.ko",
    "drivers/soc/qcom/slate_events_bridge.ko",
    "drivers/soc/qcom/slate_events_bridge_rpmsg.ko",
    "drivers/soc/qcom/slate_rsb.ko",
    "drivers/soc/qcom/slatecom_event.ko",
    "drivers/soc/qcom/slatecom_interface.ko",
    "drivers/soc/qcom/slatecom_rpmsg.ko",
    "drivers/soc/qcom/slatecom_spi.ko",
    "drivers/soc/qcom/slatersb_rpmsg.ko",
]

# Use this to ensure that either the nanohub or autobahn module--but not both--
# is part of the initramfs. Otherwise, if both modules are part of initramfs,
# they will both be loaded, and that is not supported. This ensures that the
# kernel build's images will always be usable.
_SW5100_MCU_TRANSPORT_MODULE = select({
    "//private/google-modules/soc/msm:mcu_transport_autobahn": ["autobahn.ko"],
    "//private/google-modules/soc/msm:mcu_transport_autobahn_frame_1032b": ["autobahn_frame_1032b.ko"],
    "//private/google-modules/soc/msm:mcu_transport_nanohub": ["nanohub.ko"],
})

# This list contains modules that are required for first stage init on all
# sw5100 based platforms, in addition to the modules in
# vendor_kernel_boot_modules.monaco
SW5100_VENDOR_KERNEL_BOOT_MODULES = _SW5100_MCU_TRANSPORT_MODULE + [
    # keep sorted
    "audio_pkt_dlkm.ko",
    "audpkt_ion_dlkm.ko",
    "coresight.ko",
    "gpr_dlkm.ko",
    "gpucc-monaco.ko",
    "mcu_display.ko",
    "msm_drm.ko",
    "msm_kgsl.ko",
    "msm_performance.ko",
    "msm_sysstats.ko",
    "panel_event_notifier.ko",
    "pdr_interface.ko",
    "pinctrl-spmi-gpio.ko",
    "q6_notifier_dlkm.ko",
    "q6_pdr_dlkm.ko",
    "qcom_glink_smem.ko",
    "qcom_pil_info.ko",
    "qcom_q6v5.ko",
    "qcom_q6v5_pas.ko",
    "qcom_ramdump.ko",
    "qcom_smd.ko",
    "qcom_sysmon.ko",
    "qmi_helpers.ko",
    "qrng_dlkm.ko",
    "qseecom_dlkm.ko",
    "qseecom_proxy.ko",
    "qti-qbg-main.ko",
    "qti-regmap-debugfs.ko",
    "rproc_qcom_common.ko",
    "sched-walt.ko",
    "smcinvoke_dlkm.ko",
    "snd_event_dlkm.ko",
    "spf_core_dlkm.ko",
]

# sw5100 common modules loaded during first stage init when
# booting into recovery mode from vendor_kernel_boot.
SW5100_VENDOR_KERNEL_BOOT_RECOVERY_MODULES = [
    # keep sorted
    "dwc3-msm.ko",
    "eud.ko",
    "gbms_virt_storage.ko",
    "google-battery.ko",
    "google-bms.ko",
    "google-charger.ko",
    "google-extcon-shim.ko",
    "gvotable.ko",
    "i2c-msm-geni.ko",
    "logbuffer.ko",
    "msm_gpi.ko",
    "phy-generic.ko",
    "phy-msm-snps-hs.ko",
    "qcom-spmi-adc5-gen3.ko",
    "qcom-vadc-common.ko",
    "qcom_ipc_logging.ko",
    "qpnp-power-on.ko",
    "qpnp-smblite-main.ko",
    "qti-qbg-main.ko",
    "sw5100_bms.ko",
    "wcd_usbss_i2c.ko",
]

# sw5100 common modules loaded during first stage init when booting into
# charger mode from vendor_kernel_boot.
SW5100_VENDOR_KERNEL_BOOT_CHARGER_MODULES = [
    # keep sorted
    "gbms_virt_storage.ko",
    "google-battery.ko",
    "google-bms.ko",
    "google-charger.ko",
    "gvotable.ko",
    "logbuffer.ko",
    "qcom-spmi-adc5-gen3.ko",
    "qcom-vadc-common.ko",
    "qpnp-smblite-main.ko",
    "qti-qbg-main.ko",
    "sw5100_bms.ko",
]

SW5100_VENDOR_DLKM_MODULES = [
    #keep sorted
    "adsp_loader_dlkm.ko",
    "audio_cc_ipc_dlkm.ko",
    "audio_prm_dlkm.ko",
    "besbev-slave_dlkm.ko",
    "besbev_dlkm.ko",
    "bolero_cdc_dlkm.ko",
    "cc_dlkm.ko",
    "debug_snapshot_debug_kinfo.ko",
    "frpc-adsprpc.ko",
    "gbms_virt_storage.ko",
    "google-battery.ko",
    "google-bms.ko",
    "google-charger.ko",
    "google-extcon-shim.ko",
    "google-smblite-hvdcp.ko",
    "gpio-cdev-wake.ko",
    "gsim.ko",
    "gvotable.ko",
    "hdcp_qseecom_dlkm.ko",
    "ipam.ko",
    "ipanetm.ko",
    "logbuffer.ko",
    "machine_dlkm.ko",
    "mcu_mic_codec.ko",
    "modemsmem.ko",
    "msm_ext_display.ko",
    "msm_video.ko",
    "p73.ko",
    "pinctrl_lpi_dlkm.ko",
    "pmw5100-spmi_dlkm.ko",
    "qce50_dlkm.ko",
    "qcedev-mod_dlkm.ko",
    "qcrypto-msm_dlkm.ko",
    "qpnp-smblite-main.ko",
    "restart_debug.ko",
    "rmnet_core.ko",
    "rmnet_ctl.ko",
    "rmnet_mem.ko",
    "rmnet_wlan.ko",
    "rx_macro_dlkm.ko",
    "sn220_i2c.ko",
    "stub_dlkm.ko",
    "sw5100_bms.ko",
    "swr_ctrl_dlkm.ko",
    "swr_dlkm.ko",
    "sync_fence.ko",
    "tx_macro_dlkm.ko",
    "tz_log_dlkm.ko",
    "va_macro_dlkm.ko",
    "wcd9xxx_dlkm.ko",
    "wcd_core_dlkm.ko",
    "wsa883x_dlkm.ko",
]

SW5100_VENDOR_DLKM_BLOCKLIST_MODULES = [
    #keep sorted
    "i2c-dev.ko",
    "p73.ko",
]

SW5100_COMMON_GKI_MODULES = [
    # keep sorted
    "drivers/block/zram/zram.ko",
    "mm/zsmalloc.ko",
    "net/rfkill/rfkill.ko",
    "net/tipc/tipc.ko",
]

SW5100_KUNIT_GKI_MODULES = [
    # keep sorted
    "lib/kunit/kunit.ko",
]

def get_sw5100_in_tree_modules():
    """Generates a list of all in-tree modules for sw5100 based devices.

    Returns:
        A list containing the sw5100 SoC modules built from the device-kernel tree.
    """

    sw5100_modules = []

    for mod in monaco_in_tree_modules:
        if mod not in _SW5100_MODULES_EXCLUDE_LIST:
            sw5100_modules.append(mod)

    sw5100_modules.extend(_SW5100_ADDITIONAL_IN_TREE_MODULES)

    return sw5100_modules
