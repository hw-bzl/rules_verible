"""Helper for discovering Verilog source files on a `VerilogInfo` target."""

load("@rules_verilog//verilog:verilog_info.bzl", "VerilogInfo")

def find_srcs(target, *, include_hdrs = False):
    """Return the direct source files on a `VerilogInfo`-providing target.

    Generated files and files from external workspaces are skipped — the
    rules only operate on first-party source files.

    Args:
        target: A configured target that may carry `VerilogInfo`.
        include_hdrs: If True, also include the target's hdrs.

    Returns:
        depset[File]: the files to lint/format for this target.
    """
    if VerilogInfo not in target:
        return depset()
    if target.label.workspace_root.startswith("external"):
        return depset()
    info = target[VerilogInfo]
    direct = [f for f in info.srcs.to_list() if f.is_source]
    if include_hdrs:
        direct += [f for f in info.hdrs.to_list() if f.is_source]
    return depset(direct)
