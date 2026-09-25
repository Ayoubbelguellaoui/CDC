#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "util/temp_file.h"

#ifndef _WIN32
#include <sys/wait.h>
#endif

static std::string bin_path() {
    // OPENCDC_BIN_SUFFIX is "/<Config>" on multi-config generators (Visual
    // Studio puts binaries in src/Release/) and empty on single-config ones.
    // All paths use forward slashes (valid on Windows too).
    std::string p = std::string(OPENCDC_BIN_DIR) + OPENCDC_BIN_SUFFIX + "/opencdc";
#ifdef _WIN32
    p += ".exe";
#endif
    return "\"" + p + "\"";
}

// Null redirect for shell-out commands. Windows runs without a shell
// (run_command uses CreateProcess), so no redirection applies there.
static std::string null_redirect() {
#ifdef _WIN32
    return "";
#else
    return " > /dev/null";
#endif
}

static int exit_status(int rc) {
#ifdef _WIN32
    return rc;
#else
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

// Runs an already-built command line and returns the child's exit code.
//
// Windows: CreateProcess, no shell. cmd.exe's /c quote processing falls
// back to "strip the first and last quote characters of the whole
// command" whenever the command contains more than one quoted segment,
// which mangled every multi-argument command here into a single
// unrunnable program token (observed as exit 1 for all of them).
// Executing directly also makes paths with spaces safe and yields the
// child's exit code unwrapped.
// POSIX: std::system; callers pass the result through exit_status().
static int run_command(const std::string& cmd) {
#ifdef _WIN32
    std::string command_line = cmd;
    STARTUPINFOA info{};
    info.cb = sizeof(info);
    PROCESS_INFORMATION proc{};
    if (!::CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          nullptr, &info, &proc)) {
        return -1;
    }
    ::WaitForSingleObject(proc.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(proc.hProcess, &code);
    ::CloseHandle(proc.hThread);
    ::CloseHandle(proc.hProcess);
    return static_cast<int>(code);
#else
    return std::system(cmd.c_str());
#endif
}

TEST(CliTest, NoArgsPrintsUsage) {
    int rc = run_command(bin_path().c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, HelpExitsZero) {
    int rc = run_command((bin_path() + " --help").c_str());
    EXPECT_EQ(rc, 0);
}

TEST(CliTest, CheckWithoutTopFails) {
    int rc = run_command((bin_path() + " check foo.sv").c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, CheckWithoutFilesFails) {
    int rc = run_command((bin_path() + " check --top mod").c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, SignoffClean2ffExitsZero) {
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/sync_2ff.sv\" --top sync_2ff --signoff --profile asic_signoff";
    int rc = run_command(cmd.c_str());
    EXPECT_EQ(rc, 0);
}

TEST(CliTest, SignoffUnsyncExitsOne) {
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/cdc_crossing.sv\" --top simple_cdc_crossing --signoff";
    int rc = run_command(cmd.c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, LspHelpExitsZero) {
    int rc = run_command((bin_path() + " lsp --help").c_str());
    EXPECT_EQ(rc, 0);
}

TEST(CliTest, MissingOptionValueIsInputError) {
    int rc = run_command((bin_path() + " check foo.sv --top").c_str());
    EXPECT_EQ(exit_status(rc), 2);
}

TEST(CliTest, LspUnknownOptionIsInputError) {
    int rc = run_command((bin_path() + " lsp --bogus-flag").c_str());
    EXPECT_EQ(exit_status(rc), 2);
}

TEST(CliTest, CompareMissingBaselineIsInputError) {
    std::string missing =
        opencdc::util::unique_temp_path("definitely_missing_baseline_xyz", ".json");
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/cdc_crossing.sv\" --top simple_cdc_crossing "
                      "--compare-baseline \"" +
                      missing + "\"";
    int rc = run_command(cmd.c_str());
    EXPECT_EQ(exit_status(rc), 2);
}

TEST(CliTest, FalsePathWithExtraColonsAccepted) {
    // Split on last colon: must not be rejected as malformed (exit 2);
    // runs to findings (exit 1) instead.
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/cdc_crossing.sv\" --top simple_cdc_crossing "
                      "--false-path \"a:b:c\" --format json" +
                      null_redirect();
    int rc = run_command(cmd.c_str());
    EXPECT_EQ(exit_status(rc), 1);
}
