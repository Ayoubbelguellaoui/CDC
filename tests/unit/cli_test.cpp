#include <gtest/gtest.h>
#include <cstdlib>
#include <string>

static std::string bin_path() {
    std::string p = std::string(OPENCDC_BIN_DIR) + "/opencdc";
    return "\"" + p + "\"";
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
    std::string cmd = bin_path() + " check " + std::string(FIXTURES_DIR) +
                      "/sv/sync_2ff.sv --top sync_2ff --signoff --profile asic_signoff";
    int rc = std::system(cmd.c_str());
    EXPECT_EQ(rc, 0);
}

TEST(CliTest, SignoffUnsyncExitsOne) {
    std::string cmd = bin_path() + " check " + std::string(FIXTURES_DIR) +
                      "/sv/cdc_crossing.sv --top simple_cdc_crossing --signoff";
    int rc = std::system(cmd.c_str());
    EXPECT_NE(rc, 0);
}

TEST(CliTest, LspHelpExitsZero) {
    int rc = std::system((bin_path() + " lsp --help").c_str());
    EXPECT_EQ(rc, 0);
}
