# SPDX-License-Identifier: GPL-2.0-or-later

"""Merges the external modules UAPI headers with the core kernel UAPI headers into a tarball."""

load(
    "@bazel_skylib//lib:paths.bzl",
    "paths",
)
load(
    "@kleaf//build/kernel/kleaf:hermetic_tools.bzl",
    "hermetic_toolchain",
)

def _merged_external_kernel_modules_uapi_headers(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)

    # Using "kernel-uapi-headers.tar.gz" to remain compatible
    # with the existing tarball name.
    out_uapi_headers_tarball_name = "kernel-uapi-headers.tar.gz"
    out_file = ctx.actions.declare_file(out_uapi_headers_tarball_name)
    inputs = []
    outputs = [out_file]

    for f in ctx.files.merged_kernel_uapi_headers:
        if f.basename == "kernel-uapi-headers.tar.gz":
            inputs.append(f)

    for f in ctx.files.external_modules_kernel_uapi_headers:
        if f.basename.replace("_", "-").endswith("uapi-headers.tar.gz"):
            inputs.append(f)

    intermediates_dir = paths.join(
        ctx.bin_dir.path,
        paths.dirname(ctx.build_file_path),
        ctx.attr.name + "_intermediates",
    )

    command = hermetic_tools.setup
    command += """
        # Extract all UAPI headers
        mkdir -p {intermediates_dir}

        all_uapi_headers_archives=({all_uapi_headers_archives})

        # Unpack and repack all archives to combine them
        for archive in "${{all_uapi_headers_archives[@]}}"; do
            tar xf ${{archive}} -C {intermediates_dir}
        done

        tar czf {out_name} -C {intermediates_dir} usr
    """.format(
        intermediates_dir = intermediates_dir,
        all_uapi_headers_archives = " ".join([archive.path for archive in inputs]),
        out_name = out_file.path,
    )

    ctx.actions.run_shell(
        mnemonic = "MergedExternalKernelModulesUAPIHeaders",
        inputs = inputs,
        outputs = outputs,
        tools = hermetic_tools.deps,
        progress_message = "Merging external kernel modules UAPI headers",
        command = command,
    )

    return [DefaultInfo(files = depset(outputs))]

merged_external_kernel_modules_uapi_headers = rule(
    doc = """Merges the UAPI headers from external modules with the UAPI headers from the kernel""",
    implementation = _merged_external_kernel_modules_uapi_headers,
    attrs = {
        "merged_kernel_uapi_headers": attr.label(
            doc = "The UAPI headers from the core-kernel and in-tree UAPI headers merged together.",
            allow_files = True,
            mandatory = True,
        ),
        "external_modules_kernel_uapi_headers": attr.label_list(
            doc = "A list of labels referring to ddk_uapi_headers() targets for external modules.",
            allow_files = True,
            mandatory = True,
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
