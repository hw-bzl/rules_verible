# rules_verible

Bazel rules that wrap the [Verible](https://github.com/chipsalliance/verible)
SystemVerilog/Verilog tools — `verible-verilog-format`, `verible-verilog-lint`,
`verible-verilog-diff`, and `verible-verilog-syntax` — for projects that build with
[rules_verilog](https://github.com/hw-bzl/rules_verilog).

## Setup

Add to `MODULE.bazel`:

```python
bazel_dep(name = "rules_verilog", version = "1.4.3")
bazel_dep(name = "rules_verible", version = "{see_releases}")
```

Use in a `BUILD.bazel`:

```python
load("@rules_verilog//verilog:defs.bzl", "verilog_library")
load("@rules_verible//verible:defs.bzl", "verible_format_test", "verible_lint_test")

verilog_library(
    name = "adder",
    srcs = ["adder.sv"],
)

verible_format_test(
    name = "adder_format_test",
    target = ":adder",
)

verible_lint_test(
    name = "adder_lint_test",
    target = ":adder",
)
```

## Documentation

Full rule documentation, the `.bazelrc` setup for aspect-driven checks,
toolchain overrides, and the fixer commands are at
<https://hw-bzl.github.io/rules_verible/>.
