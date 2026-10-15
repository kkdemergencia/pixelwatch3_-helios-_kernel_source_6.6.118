# SPDX-License-Identifier: GPL-2.0

seluna_lte_overlays := \
	selene-nff-v2.0-overlay.dtbo \
	selene-nff-v2.1-overlay.dtbo \
	selene-proto-v1.0-overlay.dtbo \
	selene-proto-v1.1-overlay.dtbo \
	selene-evt-v1.0-overlay.dtbo \
	selene-evt-v1.5-overlay.dtbo \
	selene-dvt-v1.0-overlay.dtbo \
	luna-nff-v2.0-overlay.dtbo \
	luna-nff-v2.1-overlay.dtbo \
	luna-proto-v1.0-overlay.dtbo \
	luna-proto-v1.1-overlay.dtbo \
	luna-evt-v1.0-overlay.dtbo \
	luna-evt-v1.5-overlay.dtbo \
	luna-dvt-v1.0-overlay.dtbo \

seluna_lte_dtb := ../google-dts-base/sw5100.dtb

seluna_btwifi_overlays := \
	helios-nff-v2.0-overlay.dtbo \
	helios-nff-v2.1-overlay.dtbo \
	helios-proto-v1.0-overlay.dtbo \
	helios-proto-v1.1-overlay.dtbo \
	helios-evt-v1.0-overlay.dtbo \
	helios-evt-v1.5-overlay.dtbo \
	helios-dvt-v1.0-overlay.dtbo \
	sol-nff-v2.0-overlay.dtbo \
	sol-nff-v2.1-overlay.dtbo \
	sol-proto-v1.0-overlay.dtbo \
	sol-proto-v1.1-overlay.dtbo \
	sol-evt-v1.0-overlay.dtbo \
	sol-evt-v1.5-overlay.dtbo \
	sol-dvt-v1.0-overlay.dtbo \

seluna_btwifi_dtb := ../google-dts-base/sw5100-btwifi.dtb

# Usage:
#   $(call DTBS_overlay_template, overlay, base_dtb, suffix)
#
# This is a generic template to create the following construct:
#   <overlay>-<suffix>-dtbs := <base_dtb>.dtb <overlay>.dtbo
define DTBS_overlay_template
	$(1:%.dtbo=%-$(strip $3)-dtbs) := $2 $1
endef

# Usage:
#   $(call DTB_object_template, CONFIG_, overlay, suffix)
#
# This is a generic template to create the following construct:
#   dtb-$(CONFIG_) += <overlay>-<suffix>.dtb
define DTB_object_template
	dtb-$(strip $1) += $(2:%.dtbo=%-$(strip $3).dtb)
endef

# Usage:
#   $(call multi_dtbs_overlay, list_of_base_dtbs, list_of_overlays, CONFIG_)
#
# For each (base_dtb,overlay) pair, this will create the following constructs:
#   <overlay>-<base_dtb>-dtbs := <base_dtb>.dtb <overlay>.dtbo
#   dtb-$(CONFIG_) += <overlay>-<base_dtb>.dtb
define multi_dtbs_overlay
	$(foreach base, $1, \
		$(foreach overlay, $2, \
			$(eval $(call DTBS_overlay_template, $(overlay), $(base), $(base:%.dtb=%))) \
			$(eval $(call DTB_object_template, $3, $(overlay), $(base:%.dtb=%)))))
endef

# Create the -dtbs and dtb-y constructs. We can do this by looping through the
# base dtbs and apply each overlay to it. Kbuild will use these -dtbs
# constructs to determine which dtbs are the base dtbs and which are the
# overlays. For the base dtbs, kbuild will expose the symbols as needed for the
# overlays. Additionally, kbuild will statically apply the overlay to the base
# dtb which could be used to verify the overlay applies cleanly.
$(call multi_dtbs_overlay, $(seluna_lte_dtb), $(seluna_lte_overlays), y)
$(call multi_dtbs_overlay, $(seluna_btwifi_dtb), $(seluna_btwifi_overlays), y)
