"""Module extension that fetches Verible release tarballs."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# The single Verible release we ship per rules_verible version. Bump in lockstep
# with the version in version.bzl.
_VERIBLE_VERSION = "v0.0-4053-g89d4d98a"

_RELEASE_URL = "https://github.com/chipsalliance/verible/releases/download/{ver}/{asset}"

# Per-platform asset metadata.
#
# tuple = (asset_filename, strip_prefix, sha256)
#
# The upstream tarballs have inconsistent root directory names — linux uses
# `verible-{ver}/`, macOS uses `verible-{ver}-macOS/`, win64 uses
# `verible-{ver}-win64/`. We capture each.
_PLATFORMS = {
    "verible_linux_aarch64": (
        "verible-{ver}-linux-static-arm64.tar.gz",
        "verible-{ver}",
        "e6184011e93eb843fe0b5f1ecc60dcb06eec0ca05784f5caff1a17814068bca1",
    ),
    "verible_linux_x86_64": (
        "verible-{ver}-linux-static-x86_64.tar.gz",
        "verible-{ver}",
        "1edc1f29c70d74213ed373e727183802d5a733e23f9ab9c74462f5b18b76f2c0",
    ),
    "verible_macos": (
        "verible-{ver}-macOS.tar.gz",
        "verible-{ver}-macOS",
        "6eb2ed4f443baed841159f3b23ebebd70d2fde789e64f6f3e2baa02ef73a0ddd",
    ),
    "verible_windows_x86_64": (
        "verible-{ver}-win64.zip",
        "verible-{ver}-win64",
        "83d20f51b6092c3b62a68c350520d6f45b97fda7332bc3320a2fdc7ab37109a3",
    ),
}

def _verible_impl(ctx):
    for repo_name, (asset_template, strip_template, sha256) in _PLATFORMS.items():
        asset = asset_template.format(ver = _VERIBLE_VERSION)
        http_archive(
            name = repo_name,
            url = _RELEASE_URL.format(ver = _VERIBLE_VERSION, asset = asset),
            sha256 = sha256,
            strip_prefix = strip_template.format(ver = _VERIBLE_VERSION),
            build_file = "@rules_verible//verible/private:BUILD.verible.bazel",
        )
    return ctx.extension_metadata(reproducible = True)

verible = module_extension(
    doc = "TODO",
    implementation = _verible_impl,
)
