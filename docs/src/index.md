# rules_verible

Bazel rules that wrap the [Verible](https://github.com/chipsalliance/verible)
SystemVerilog/Verilog tools — `verible-verilog-format`, `verible-verilog-lint`,
and `verible-verilog-diff` — for projects that build with
[rules_verilog](https://github.com/hw-bzl/rules_verilog).

## Overview

Each tool ships as a combination of an aspect (run via
`bazel build --config=...`), a test rule, and a fixer binary (run via
`bazel run`). Process wrappers are written in C++ (POSIX + Win32) so the rule
set works on every platform Verible itself ships for, with no scripting-language
dependency.

- **`verible_toolchain`** — registers the three Verible binaries
  (`verible-verilog-format`, `verible-verilog-lint`, `verible-verilog-diff`).
  Default toolchains are auto-registered per-platform using prebuilt release
  tarballs.
- **`verible_format_aspect`** / **`verible_format_test`** — check that
  `VerilogInfo` targets are correctly formatted.
- **`verible_lint_aspect`** / **`verible_lint_test`** — lint `VerilogInfo`
  targets.
- **`verible_diff_test`** — token-level diff between two Verilog/SystemVerilog
  files (`format` mode by default).
- `bazel run //verible:format_fix -- //...` — rewrites Verilog sources in-place
  via `verible-verilog-format --inplace`.
- `bazel run //verible:lint_fix   -- //...` — applies `verible-verilog-lint
  --autofix=inplace` across the workspace.

Rule API details are generated from the Starlark sources in the sections linked
from [Summary](./SUMMARY.md).

## Quick start

Add to `MODULE.bazel`:

```python
bazel_dep(name = "rules_verilog", version = "1.1.1")
bazel_dep(name = "rules_verible", version = "{version}")
```

Then in a `BUILD.bazel`:

```python
load("@rules_verilog//verilog:defs.bzl", "verilog_library")
load("@rules_verible//verible:defs.bzl", "verible_format_test", "verible_lint_test")

verilog_library(name = "adder", srcs = ["adder.sv"])

verible_format_test(name = "adder_format_test", target = ":adder")
verible_lint_test(name = "adder_lint_test", target = ":adder")
```

## Enabling the aspects in your `.bazelrc`

The aspect-driven checks (the `--config=verible_format`, `--config=verible_lint`,
and combined `--config=verible` flags) are not auto-registered downstream. Add
the following block to your repo's `.bazelrc` to opt in:

```text
# Enable verible-verilog-format checks for all targets in the workspace.
# Usage: bazel build --config=verible_format //...
build:verible_format --aspects=@rules_verible//verible:verible_format_aspect.bzl%verible_format_aspect
build:verible_format --output_groups=+verible_format_checks

# Enable verible-verilog-lint checks for all targets in the workspace.
# Usage: bazel build --config=verible_lint //...
build:verible_lint --aspects=@rules_verible//verible:verible_lint_aspect.bzl%verible_lint_aspect
build:verible_lint --output_groups=+verible_lint_checks

# Composite: run both at once.
# Usage: bazel build --config=verible //...
build:verible --config=verible_format
build:verible --config=verible_lint
```

Aspects propagate across every transitive `VerilogInfo`-providing dependency,
so a single `bazel build --config=verible //some:top_target` checks the whole
graph reachable from `//some:top_target`. To exclude a `verilog_library` from
the sweep, tag it with one of:

| Tag                  | Effect                          |
|----------------------|---------------------------------|
| `noformat`           | Skip the format aspect.         |
| `no-format`          | Same as `noformat`.             |
| `no-verible-format`  | Same; verible-specific spelling.|
| `nolint`             | Skip the lint aspect.           |
| `no-lint`            | Same as `nolint`.               |
| `no-verible-lint`    | Same; verible-specific spelling.|
| `no-verible`         | Skip both aspects.              |

The `bazel run //verible:format_fix` / `lint_fix` fixer binaries honor the same
tag set when discovering Verilog sources via `bazel query`.

## Where the Verible binaries come from

Verible binaries come from the upstream
[GitHub releases](https://github.com/chipsalliance/verible/releases) via a
module extension; supported platforms are linux x86_64, linux aarch64, macOS,
and windows x86_64. To override (e.g. point at a system-installed or vendored
Verible), register your own `verible_toolchain(...)` instance ahead of the
defaults — see the [`verible_toolchain`](./verible_toolchain.md) page.
