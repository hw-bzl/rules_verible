"""Implementation of `verible_format_aspect` and `verible_format_test`."""

load("@rules_verilog//verilog:defs.bzl", "VerilogInfo")
load("//verible:verible_toolchain.bzl", "TOOLCHAIN_TYPE")
load("//verible/private:target_srcs.bzl", "find_srcs")

# Skip targets carrying any of these tags.
_SKIP_TAGS = ["noformat", "no-format", "no-verible-format", "no-verible"]

def _has_skip_tag(ctx):
    tags = getattr(ctx.rule.attr, "tags", [])
    for t in tags:
        if t in _SKIP_TAGS:
            return True
    return False

def _rlocationpath(file, workspace_name):
    if file.short_path.startswith("../"):
        return file.short_path[len("../"):]

    return "{}/{}".format(workspace_name, file.short_path)

def _verible_format_aspect_impl(target, ctx):
    if VerilogInfo not in target:
        return []
    if _has_skip_tag(ctx):
        return []
    srcs = find_srcs(target).to_list()
    if not srcs:
        return []

    tc = ctx.toolchains[TOOLCHAIN_TYPE]
    marker = ctx.actions.declare_file("{}.verible_format.ok".format(target.label.name))

    args = ctx.actions.args()
    args.add("--verible-format=" + tc.verible_format.path)
    args.add("--marker=" + marker.path)
    args.add_all(srcs, format_each = "--src=%s")
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset(srcs, transitive = [tc.all_files]),
        tools = [tc.verible_format],
        outputs = [marker],
        mnemonic = "VeribleFormat",
        progress_message = "verible-verilog-format %{label}",
    )
    return [OutputGroupInfo(verible_format_checks = depset([marker]))]

verible_format_aspect = aspect(
    implementation = _verible_format_aspect_impl,
    doc = "Aspect that runs `verible-verilog-format --verify` on every `VerilogInfo` target.",
    attrs = {
        "_runner": attr.label(
            cfg = "exec",
            executable = True,
            default = Label("//verible/private:verible_format_runner"),
        ),
    },
    toolchains = [TOOLCHAIN_TYPE],
)

def _verible_format_test_impl(ctx):
    target = ctx.attr.target
    srcs = find_srcs(target).to_list()
    if not srcs:
        fail("verible_format_test.target has no first-party Verilog srcs: {}".format(target.label))

    tc = ctx.toolchains[TOOLCHAIN_TYPE]
    config = ctx.file.config
    ws = ctx.workspace_name

    # Build the args file the runner reads at test time. Values are runfiles
    # keys (rlocationpaths); the runner resolves them via @rules_cc//cc/runfiles
    # when VERIBLE_FORMAT_TEST_ARGS_FILE is set.
    args = ctx.actions.args()
    args.set_param_file_format("multiline")
    args.add(_rlocationpath(tc.verible_format, ws), format = "--verible-format=%s")
    if config:
        args.add(_rlocationpath(config, ws), format = "--config=%s")
    args.add_all(
        [_rlocationpath(src, ws) for src in srcs],
        format_each = "--src=%s",
    )

    args_file = ctx.actions.declare_file("{}.args.txt".format(ctx.label.name))
    ctx.actions.write(
        output = args_file,
        content = args,
    )

    # Bazel requires a test rule's executable be created by the rule itself, so
    # symlink the runner cc_binary into this package and surface that. Preserve
    # any Windows `.exe` / `.bat` extension so the OS still treats it as an
    # executable after the symlink.
    extension = ""
    if ctx.executable._runner.basename.endswith((".exe", ".bat")):
        extension = ".{}".format(ctx.executable._runner.extension)

    test_bin = ctx.actions.declare_file("{}{}".format(ctx.label.name, extension))
    ctx.actions.symlink(
        output = test_bin,
        target_file = ctx.executable._runner,
        is_executable = True,
    )

    runner_default_runfiles = ctx.attr._runner[DefaultInfo].default_runfiles
    runfiles = ctx.runfiles(
        files = list(srcs) + ([config] if config else []) + [
            tc.verible_format,
            ctx.executable._runner,
            args_file,
        ],
        transitive_files = tc.all_files,
    ).merge(runner_default_runfiles)

    return [
        DefaultInfo(
            executable = test_bin,
            runfiles = runfiles,
        ),
        RunEnvironmentInfo(
            environment = {
                "VERIBLE_FORMAT_TEST_ARGS_FILE": _rlocationpath(args_file, ws),
            },
        ),
    ]

verible_format_test = rule(
    implementation = _verible_format_test_impl,
    doc = "Test rule that runs `verible-verilog-format --verify` against a `VerilogInfo` target.",
    attrs = {
        "config": attr.label(
            allow_single_file = True,
            doc = "Optional verible-verilog-format flagfile.",
        ),
        "target": attr.label(
            mandatory = True,
            providers = [VerilogInfo],
            doc = "The `verilog_library`-like target whose srcs should be format-checked.",
        ),
        "_runner": attr.label(
            cfg = "exec",
            executable = True,
            default = Label("//verible/private:verible_format_runner"),
        ),
    },
    test = True,
    toolchains = [TOOLCHAIN_TYPE],
)
