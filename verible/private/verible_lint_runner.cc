/// @file
/// @brief Process wrapper invoked by the `verible_lint` aspect and test rule.
///
/// @details
///   Runs `verible-verilog-lint` against a list of source files and, on
///   success, touches a marker file so Bazel records the action as cached.
///   When a non-zero exit code is observed the captured stdout/stderr from
///   the child is forwarded to stderr.
///
///   The runner operates in one of two modes, chosen automatically:
///   - **Test mode**: the env var `VERIBLE_LINT_TEST_ARGS_FILE` holds the
///     runfiles key of an args file. The runner resolves that file via
///     `@rules_cc//cc/runfiles`, then resolves every PATH-valued flag inside
///     (`--verible-lint=`, `--src=`, `--rules-config=`, `--flagfile=`) the
///     same way. This path works in both symlink-tree and manifest-only
///     runfiles modes.
///   - **Aspect / CLI mode**: argv (or `@argfile`) holds literal sandbox paths
///     directly, exactly as Bazel built them.
///
/// @par CLI
///   | Flag                    | Description                                          |
///   |-------------------------|------------------------------------------------------|
///   | `--verible-lint=PATH`   | (required) Path to the verible-verilog-lint binary.  |
///   | `--marker=PATH`         | (optional) File to touch on success.                 |
///   | `--rules-config=PATH`   | (optional) Passed through as `--rules_config=PATH`.  |
///   | `--flagfile=PATH`       | (repeatable) Passed through as `--flagfile=PATH`.    |
///   | `--src=PATH`            | (repeatable) Source file to lint.                    |
///   | `--`                    | End of options.                                      |
///   | `[extra]`               | Pass-through args appended to the verible command.   |

#include "rules_cc/cc/runfiles/runfiles.h"
#include "verible/private/subprocess.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using rules_cc::cc::runfiles::Runfiles;
namespace sp = rules_verible::subprocess;

namespace {

/// @brief Print @p msg to stderr and exit with status 2.
[[noreturn]] void die(const std::string& msg) {
  std::fprintf(stderr, "verible_lint_runner: %s\n", msg.c_str());
  std::exit(2);
}

/// @brief Load one argument per line from a Bazel parameter file.
std::vector<std::string> read_param_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) die("could not open param file: " + path);
  std::vector<std::string> out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
  }
  return out;
}

/// @brief Test whether @p s begins with @p prefix.
bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// @brief Extract the value from a `--flag=value` argument.
std::string flag_value(const std::string& arg, const std::string& flag) {
  return arg.substr(flag.size() + 1);
}

/// @brief Create or truncate @p path so its mtime is updated.
void touch_file(const std::string& path) {
  std::ofstream(path, std::ios::out | std::ios::trunc).close();
}

/// @brief Resolve @p key via @p runfiles if non-null, otherwise return @p key.
std::string maybe_resolve(Runfiles* runfiles, const std::string& key) {
  if (!runfiles) return key;
  std::string resolved = runfiles->Rlocation(key);
  if (resolved.empty()) die("could not resolve runfiles key: " + key);
  return resolved;
}

}  // namespace

/// @brief Program entry point. See file-level documentation for the CLI.
int main(int argc, char** argv) {
  std::unique_ptr<Runfiles> runfiles;
  std::vector<std::string> args;

  std::string test_args_key = sp::get_env("VERIBLE_LINT_TEST_ARGS_FILE");
  if (!test_args_key.empty()) {
    std::string err;
    runfiles.reset(Runfiles::Create(argv[0], BAZEL_CURRENT_REPOSITORY, &err));
    if (!runfiles) die("could not initialize runfiles: " + err);

    std::string args_file_path = runfiles->Rlocation(test_args_key);
    if (args_file_path.empty()) {
      die("could not resolve VERIBLE_LINT_TEST_ARGS_FILE='" + test_args_key + "'");
    }
    args = read_param_file(args_file_path);
  } else if (argc == 2 && argv[1][0] == '@') {
    args = read_param_file(argv[1] + 1);
  } else {
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  }

  std::string verible_bin;
  std::string marker;
  std::string rules_config;
  std::vector<std::string> flagfiles;
  std::vector<std::string> srcs;
  std::vector<std::string> passthrough;
  bool after_dashdash = false;

  for (const auto& a : args) {
    if (after_dashdash) {
      passthrough.push_back(a);
      continue;
    }
    if (a == "--") {
      after_dashdash = true;
    } else if (starts_with(a, "--verible-lint=")) {
      verible_bin = maybe_resolve(runfiles.get(), flag_value(a, "--verible-lint"));
    } else if (starts_with(a, "--marker=")) {
      marker = flag_value(a, "--marker");
    } else if (starts_with(a, "--rules-config=")) {
      rules_config = maybe_resolve(runfiles.get(), flag_value(a, "--rules-config"));
    } else if (starts_with(a, "--flagfile=")) {
      flagfiles.push_back(maybe_resolve(runfiles.get(), flag_value(a, "--flagfile")));
    } else if (starts_with(a, "--src=")) {
      srcs.push_back(maybe_resolve(runfiles.get(), flag_value(a, "--src")));
    } else {
      die("unknown argument: " + a);
    }
  }

  if (verible_bin.empty()) die("--verible-lint=PATH is required");
  if (srcs.empty()) die("at least one --src=PATH is required");

  std::vector<std::string> cmd;
  cmd.reserve(srcs.size() + flagfiles.size() + 4);
  cmd.push_back(verible_bin);
  if (!rules_config.empty()) cmd.push_back("--rules_config=" + rules_config);
  for (const auto& f : flagfiles) cmd.push_back("--flagfile=" + f);
  for (const auto& s : srcs) cmd.push_back(s);
  for (const auto& p : passthrough) cmd.push_back(p);

  // Let the child's stdout/stderr stream straight through. Bazel buffers
  // action and test output itself and only surfaces it on failure, so there's
  // no value in capturing inside the runner.
  int rc = sp::run_inherit(cmd, "");
  if (rc == 0 && !marker.empty()) touch_file(marker);
  return rc;
}
