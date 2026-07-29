/// @file
/// @brief Workspace formatter invoked via `bazel run //verible:format_fix`.
///
/// @details
///   Discovers every Verilog source file that is a direct dep of a
///   `verilog_*` rule in scope via `bazel query` and runs
///   `verible-verilog-format --inplace` over them. Mirrors the shape
///   of rules_venv's `black_fixer.py`. The verible binary is resolved through
///   the runfiles library; the `VERIBLE_FORMAT_RLOCATIONPATH` env var supplies
///   its runfiles lookup key (set by the `cc_binary`'s `env` attribute via the
///   `current_verible_toolchain`'s `TemplateVariableInfo`). Resolving every
///   path via `Runfiles::Rlocation()` keeps the fixer working in both
///   symlink-tree and manifest-only runfiles modes.
///
/// @par CLI
///   | Flag             | Description                                            |
///   |------------------|--------------------------------------------------------|
///   | `--bazel PATH`   | (optional) Bazel binary; defaults to `$BAZEL_REAL`     |
///   |                  | then `which bazel` / `which bazelisk`.                 |
///   | `[scope ...]`    | Target patterns for the query. Default `//...:all`.    |
///
/// @par Environment
///   - `BUILD_WORKSPACE_DIRECTORY`  (required) Workspace root.
///   - `VERIBLE_FORMAT_RLOCATIONPATH` (required) Runfiles key for the binary.
///   - `BAZEL_REAL`                 (optional) Bazel binary path.
///   - `RUNFILES_DIR` / `RUNFILES_MANIFEST_FILE`  Bazel runfiles indicators.

#include "rules_cc/cc/runfiles/runfiles.h"
#include "verible/private/subprocess.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using rules_cc::cc::runfiles::Runfiles;
namespace sp = rules_verible::subprocess;

namespace {

/// @brief Print @p msg to stderr and exit with status 2.
[[noreturn]] void die(const std::string& msg) {
  std::fprintf(stderr, "verible_format_fixer: %s\n", msg.c_str());
  std::exit(2);
}

/// @brief Split @p s on '\n', dropping CR characters and empty tail lines.
std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '\n') {
      if (!cur.empty()) out.push_back(std::move(cur));
      cur.clear();
    } else if (c != '\r') {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

/// @brief Convert a Bazel label to a workspace-relative file path.
/// @details Handles `//foo:bar.sv` → `foo/bar.sv` and `//:bar.sv` → `bar.sv`.
///          External labels (`@repo//...`) are not supported here.
std::string pathify(const std::string& label) {
  if (label.size() < 2 || label[0] != '/' || label[1] != '/') {
    die("unexpected label (cannot pathify): " + label);
  }
  if (label.compare(0, 3, "//:") == 0) return label.substr(3);
  std::string body = label.substr(2);
  for (char& c : body) {
    if (c == ':') {
      c = '/';
      break;
    }
  }
  return body;
}

}  // namespace

/// @brief Program entry point. See file-level documentation for the CLI.
int main(int argc, char** argv) {
  std::string bazel_flag;
  std::vector<std::string> scope;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--bazel" && i + 1 < argc) {
      bazel_flag = argv[++i];
    } else if (a.rfind("--bazel=", 0) == 0) {
      bazel_flag = a.substr(std::string("--bazel=").size());
    } else {
      scope.push_back(a);
    }
  }
  if (scope.empty()) scope.push_back("//...:all");

  std::string workspace = sp::get_env("BUILD_WORKSPACE_DIRECTORY");
  if (workspace.empty()) {
    die("BUILD_WORKSPACE_DIRECTORY is not set. Run via `bazel run`.");
  }

  std::string bazel = !bazel_flag.empty() ? bazel_flag : sp::get_env("BAZEL_REAL");
  if (bazel.empty()) bazel = sp::which({"bazel", "bazelisk"});
  if (bazel.empty()) die("could not locate a bazel binary");

  std::string err;
  std::unique_ptr<Runfiles> runfiles(
      Runfiles::Create(argv[0], BAZEL_CURRENT_REPOSITORY, &err));
  if (!runfiles) die("could not initialize runfiles: " + err);

  std::string rl_key = sp::get_env("VERIBLE_FORMAT_RLOCATIONPATH");
  if (rl_key.empty()) die("VERIBLE_FORMAT_RLOCATIONPATH is not set");
  std::string verible_path = runfiles->Rlocation(rl_key);
  if (verible_path.empty() || !sp::file_exists(verible_path)) {
    die("could not resolve verible-verilog-format via runfiles (rlocation key '" +
        rl_key + "' -> '" + verible_path + "')");
  }

  std::string scope_set;
  for (size_t i = 0; i < scope.size(); ++i) {
    if (i) scope_set += " ";
    scope_set += scope[i];
  }
  const std::string tag_pattern =
      R"((^\[|, )(noformat|no-format|no-verible-format|no-verible)(, |\]$))";
  const std::string file_pattern = R"(^//.*\.(sv|svh|v|vh)$)";
  const std::string verilog_rules =
      "kind(\"^verilog_.* rule$\", set(" + scope_set + "))";
  std::string query =
      "filter(\"" + file_pattern + "\","
      " kind(\"source file\","
      " deps(" + verilog_rules + " except attr(tags, \"" + tag_pattern +
      "\", " + verilog_rules + "), 1)))";

  std::vector<std::string> query_cmd = {
      bazel, "query", query,
      "--noimplicit_deps", "--keep_going",
  };

  std::string query_out;
  int qrc = sp::run_capture_stdout(query_cmd, workspace, &query_out);
  // bazel query returns 3 when `--keep_going` hits partial errors but still
  // produced a result; treat any other non-zero as fatal.
  if (qrc != 0 && qrc != 3) {
    std::fprintf(stderr, "bazel query failed (exit %d)\n", qrc);
    return qrc;
  }

  std::vector<std::string> labels = split_lines(query_out);
  std::vector<std::string> files;
  files.reserve(labels.size());
  for (const auto& lbl : labels) {
    if (lbl.empty()) continue;
    if (lbl[0] == '@') continue;  // external sources are out of scope
    files.push_back(pathify(lbl));
  }

  if (files.empty()) {
    std::fprintf(stderr, "verible_format_fixer: no source files in scope; nothing to do\n");
    return 0;
  }

  std::vector<std::string> fmt_cmd = {verible_path, "--inplace"};
  fmt_cmd.insert(fmt_cmd.end(), files.begin(), files.end());

  std::fprintf(stderr, "verible_format_fixer: formatting %zu file(s)\n", files.size());
  return sp::run_inherit(fmt_cmd, workspace);
}
