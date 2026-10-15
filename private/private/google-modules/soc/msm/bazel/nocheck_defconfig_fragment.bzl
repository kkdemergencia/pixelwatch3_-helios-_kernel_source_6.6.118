# SPDX-License-Identifier: GPL-2.0-or-later

"""File containing the implementation of the nocheck_defconfig_fragment rule."""

load(
    "@kleaf//build/kernel/kleaf:hermetic_tools.bzl",
    "hermetic_toolchain",
)

def _nocheck_defconfig_fragment_impl(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)

    out_file = ctx.actions.declare_file("{}/nocheck_defconfig.fragment".format(ctx.label.name))

    inputs = depset(
        ctx.files.base_defconfig_fragment_file,
        transitive = [target.files for target in ctx.attr.overlay_defconfig_fragment_files],
        order = "preorder",
    )

    args = ctx.actions.args()
    args.add(out_file)
    args.add_all(inputs)

    command = hermetic_tools.setup + """
        out=$1
        base_defconfig_fragment=$2
        shift 2
        overlay_files=$*

        cp $base_defconfig_fragment $out

        config_set="s/^(CONFIG_\\w*)=.*/\\1/p"
        config_not_set="s/^# (CONFIG_\\w*) is not set$/\\1/p"
        configs=$(sed -n -E -e "${config_set}" -e "${config_not_set}" $out)

        for config in ${configs}; do
          if grep -q ${config} -- $overlay_files; then
            new_config_set="s/(^${config}=.*)/\\1 # nocheck: Overridden by sw5100.fragment/"
            new_config_not_set="s/(^# "${config}" is not set$)/\\1 # nocheck: Overridden by sw5100.fragment/"
            sed -E -i -e "${new_config_set}" -e "${new_config_not_set}" $out
          fi
        done
    """

    ctx.actions.run_shell(
        mnemonic = "AddNoCheckToDefconfigFragmentBase",
        inputs = inputs,
        outputs = [out_file],
        tools = hermetic_tools.deps,
        progress_message = "Adding # nocheck lines to {}".format(ctx.file.base_defconfig_fragment_file.basename),
        command = command,
        arguments = [args],
    )

    return [DefaultInfo(files = depset([out_file]))]

nocheck_defconfig_fragment = rule(
    doc = """Add # nocheck: to lines in a base defconfig fragment

    Adds # nocheck ... to lines within a base defconfig fragment file by
    scanning a list of files that will be merged into a final defconfig file.

    Config options found in the files that will be merged and the base defconfig
    fragment will be marked as config options that need to be ignored when
    the kernel build tools check if config options have been applied correctly.
    Otherwise, the build will fail.
    """,
    implementation = _nocheck_defconfig_fragment_impl,
    attrs = {
        "base_defconfig_fragment_file": attr.label(
            doc = "A file that will be used as a base file for merging other fragments on top of.",
            allow_single_file = True,
            mandatory = True,
        ),
        "overlay_defconfig_fragment_files": attr.label_list(
            doc = "A list of files that will be applied on top of the base defconfig fragment file.",
            allow_files = True,
            mandatory = True,
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
