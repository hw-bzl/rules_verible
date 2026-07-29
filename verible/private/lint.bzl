"""Implementation of `verible_lint_aspect` and `verible_lint_test`."""

load("@rules_verilog//verilog:defs.bzl", "VerilogInfo")
load("//verible:verible_toolchain.bzl", "TOOLCHAIN_TYPE")
load("//verible/private:target_srcs.bzl", "find_srcs")

_SKIP_TAGS = ["nolint", "no-lint", "no-verible-lint", "no-verible"]

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

def _verible_lint_aspect_impl(target, ctx):
    if VerilogInfo not in target:
        return []
    if _has_skip_tag(ctx):
        return []
    srcs = find_srcs(target, include_hdrs = True).to_list()
    if not srcs:
        return []

    tc = ctx.toolchains[TOOLCHAIN_TYPE]
    marker = ctx.actions.declare_file("{}.verible_lint.ok".format(target.label.name))

    args = ctx.actions.args()
    args.add(tc.verible_lint, format = "--verible-lint=%s")
    args.add(marker, format = "--marker=%s")
    args.add_all(srcs, format_each = "--src=%s")
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset(srcs, transitive = [tc.all_files]),
        tools = [tc.verible_lint],
        outputs = [marker],
        mnemonic = "VeribleLint",
        progress_message = "verible-verilog-lint %{label}",
    )
    return [OutputGroupInfo(verible_lint_checks = depset([marker]))]

verible_lint_aspect = aspect(
    implementation = _verible_lint_aspect_impl,
    doc = "Aspect that runs `verible-verilog-lint` on every `VerilogInfo` target.",
    attrs = {
        "_runner": attr.label(
            cfg = "exec",
            executable = True,
            default = Label("//verible/private:verible_lint_runner"),
        ),
    },
    toolchains = [TOOLCHAIN_TYPE],
)

def _verible_lint_test_impl(ctx):
    target = ctx.attr.target
    srcs = find_srcs(target, include_hdrs = True).to_list()
    if not srcs:
        fail("verible_lint_test.target has no first-party Verilog srcs: {}".format(target.label))

    tc = ctx.toolchains[TOOLCHAIN_TYPE]
    config = ctx.file.config
    ws = ctx.workspace_name

    args = ctx.actions.args()
    args.set_param_file_format("multiline")
    args.add(_rlocationpath(tc.verible_lint, ws), format = "--verible-lint=%s")
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
    # symlink the runner cc_binary into this package and surface that.
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
            tc.verible_lint,
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
                "VERIBLE_LINT_TEST_ARGS_FILE": _rlocationpath(args_file, ws),
            },
        ),
    ]

verible_lint_test = rule(
    implementation = _verible_lint_test_impl,
    doc = "Test rule that runs `verible-verilog-lint` against a `VerilogInfo` target.",
    attrs = {
        "config": attr.label(
            allow_single_file = True,
            doc = "Optional `.rules.verible_lint` configuration file.",
        ),
        "target": attr.label(
            mandatory = True,
            providers = [VerilogInfo],
            doc = "The `verilog_library`-like target whose srcs should be lint-checked.",
        ),
        "_runner": attr.label(
            cfg = "exec",
            executable = True,
            default = Label("//verible/private:verible_lint_runner"),
        ),
    },
    test = True,
    toolchains = [TOOLCHAIN_TYPE],
)
