# SPDX-License-Identifier: GPL-2.0-or-later

"""Merge contents of a list of files and list of strings together into one file."""

load(
    "@kleaf//build/kernel/kleaf:hermetic_tools.bzl",
    "hermetic_toolchain",
)

def _merged_files_and_string_list_impl(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)

    out_file = ctx.actions.declare_file("{}/merged_output.txt".format(ctx.label.name))
    extra_lines_file = ctx.actions.declare_file("{}/extra_lines.txt".format(ctx.label.name))

    # Add a new line at the end to ensure that the file is POSIX compliant.
    ctx.actions.write(extra_lines_file, "\n".join(ctx.attr.extra_lines) + "\n")

    inputs = depset(
        [extra_lines_file],
        transitive = [target.files for target in ctx.attr.merge_files],
        order = "postorder",
    )

    args = ctx.actions.args()
    args.add(out_file)
    args.add_all(inputs)

    command = hermetic_tools.setup + """
        out=$1
        shift
        cat $* > $out
    """

    ctx.actions.run_shell(
        mnemonic = "MergeFilesAndDataStr",
        inputs = inputs,
        outputs = [out_file],
        tools = hermetic_tools.deps,
        progress_message = "Merging files and string list to create %{label}",
        command = command,
        arguments = [args],
    )

    return [DefaultInfo(files = depset([out_file]))]

merged_files_and_string_list = rule(
    doc = """Merges the content from files and a string list into a single file""",
    implementation = _merged_files_and_string_list_impl,
    attrs = {
        "merge_files": attr.label_list(
            doc = "A list of labels referring to files to merge",
            allow_files = True,
            mandatory = True,
        ),
        "extra_lines": attr.string_list(
            doc = "A list of strings to append to the contents of the merged files",
            mandatory = True,
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
