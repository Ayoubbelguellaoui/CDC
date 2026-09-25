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

// Null redirect + argument quoting that work on both POSIX sh and cmd.exe.
static std::string null_redirect() {
#ifdef _WIN32
    return " > NUL";
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

TEST(CliTest, NoArgsPrintsUsage) {
    int rc = std::system(bin_path().c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, HelpExitsZero) {
    int rc = std::system((bin_path() + " --help").c_str());
    EXPECT_EQ(rc, 0);
}

TEST(CliTest, CheckWithoutTopFails) {
    int rc = std::system((bin_path() + " check foo.sv").c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, CheckWithoutFilesFails) {
    int rc = std::system((bin_path() + " check --top mod").c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, SignoffClean2ffExitsZero) {
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/sync_2ff.sv\" --top sync_2ff --signoff --profile asic_signoff";
    int rc = std::system(cmd.c_str());
    EXPECT_EQ(rc, 0);
}

TEST(CliTest, SignoffUnsyncExitsOne) {
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/cdc_crossing.sv\" --top simple_cdc_crossing --signoff";
    int rc = std::system(cmd.c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, LspHelpExitsZero) {
    int rc = std::system((bin_path() + " lsp --help").c_str());
    EXPECT_EQ(rc, 0);
}

TEST(CliTest, MissingOptionValueIsInputError) {
    int rc = std::system((bin_path() + " check foo.sv --top").c_str());
    EXPECT_EQ(exit_status(rc), 2);
}

TEST(CliTest, LspUnknownOptionIsInputError) {
    int rc = std::system((bin_path() + " lsp --bogus-flag").c_str());
    EXPECT_EQ(exit_status(rc), 2);
}

TEST(CliTest, CompareMissingBaselineIsInputError) {
    std::string missing =
        opencdc::util::unique_temp_path("definitely_missing_baseline_xyz", ".json");
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/cdc_crossing.sv\" --top simple_cdc_crossing "
                      "--compare-baseline \"" +
                      missing + "\"";
    int rc = std::system(cmd.c_str());
    EXPECT_EQ(exit_status(rc), 2);
}

TEST(CliTest, FalsePathWithExtraColonsAccepted) {
    // Split on last colon: must not be rejected as malformed (exit 2);
    // runs to findings (exit 1) instead.
    std::string cmd = bin_path() + " check \"" + std::string(FIXTURES_DIR) +
                      "/sv/cdc_crossing.sv\" --top simple_cdc_crossing "
                      "--false-path \"a:b:c\" --format json" +
                      null_redirect();
    int rc = std::system(cmd.c_str());
    EXPECT_EQ(exit_status(rc), 1);
}
