#include "config/profile.h"

#include <gtest/gtest.h>

using namespace opencdc::config;

TEST(ProfileTest, DefaultProfile) {
    auto s = get_profile_settings(MethodologyProfile::Default);
    EXPECT_EQ(s.profile, MethodologyProfile::Default);
    EXPECT_FALSE(s.require_cdc_register_reset);
    EXPECT_FALSE(s.require_reset_synchronizer);
    EXPECT_TRUE(s.allow_annotation);
    EXPECT_FALSE(s.require_structural_proof);
    EXPECT_EQ(s.min_sync_stages, 2);
}

TEST(ProfileTest, StrictProfile) {
    auto s = get_profile_settings(MethodologyProfile::Strict);
    EXPECT_TRUE(s.require_cdc_register_reset);
    EXPECT_TRUE(s.require_reset_synchronizer);
    EXPECT_FALSE(s.allow_annotation);
    EXPECT_TRUE(s.require_structural_proof);
    EXPECT_EQ(s.min_sync_stages, 3);
    EXPECT_FALSE(s.severity_overrides.empty());
}

TEST(ProfileTest, AsicSignoffProfile) {
    auto s = get_profile_settings(MethodologyProfile::AsicSignoff);
    EXPECT_TRUE(s.require_cdc_register_reset);
    EXPECT_TRUE(s.require_structural_proof);
    EXPECT_EQ(s.min_sync_stages, 3);
    EXPECT_EQ(s.reconvergence_depth, 16);
}

TEST(ProfileTest, FpgaProfile) {
    auto s = get_profile_settings(MethodologyProfile::Fpga);
    EXPECT_FALSE(s.require_cdc_register_reset);
    EXPECT_TRUE(s.suppress_reset_crossings);
    EXPECT_FALSE(s.disabled_rules.empty());
}

TEST(ProfileTest, IpDevelopmentProfile) {
    auto s = get_profile_settings(MethodologyProfile::IpDevelopment);
    EXPECT_FALSE(s.require_cdc_register_reset);
    EXPECT_TRUE(s.suppress_reset_crossings);
}

TEST(ProfileTest, SocIntegrationProfile) {
    auto s = get_profile_settings(MethodologyProfile::SocIntegration);
    EXPECT_FALSE(s.suppress_reset_crossings);
    EXPECT_EQ(s.reconvergence_depth, 10);
}

TEST(ProfileTest, NameRoundTrip) {
    EXPECT_EQ(profile_name(MethodologyProfile::Default), "default");
    EXPECT_EQ(profile_name(MethodologyProfile::Strict), "strict");
    EXPECT_EQ(profile_name(MethodologyProfile::AsicSignoff), "asic_signoff");
    EXPECT_EQ(profile_name(MethodologyProfile::Fpga), "fpga");
    EXPECT_EQ(profile_name(MethodologyProfile::IpDevelopment), "ip_development");
    EXPECT_EQ(profile_name(MethodologyProfile::SocIntegration), "soc_integration");
}

TEST(ProfileTest, ProfileFromName) {
    EXPECT_EQ(profile_from_name("default"), MethodologyProfile::Default);
    EXPECT_EQ(profile_from_name("strict"), MethodologyProfile::Strict);
    EXPECT_EQ(profile_from_name("asic_signoff"), MethodologyProfile::AsicSignoff);
    EXPECT_EQ(profile_from_name("fpga"), MethodologyProfile::Fpga);
    EXPECT_EQ(profile_from_name("ip_development"), MethodologyProfile::IpDevelopment);
    EXPECT_EQ(profile_from_name("soc_integration"), MethodologyProfile::SocIntegration);
    EXPECT_EQ(profile_from_name("unknown"), MethodologyProfile::Default);
}

TEST(ProfileTest, ApplyProfileModifiesConfig) {
    Config cfg;
    ProfileSettings strict = get_profile_settings(MethodologyProfile::Strict);
    apply_profile(strict, cfg);

    EXPECT_TRUE(cfg.reset_policy.require_cdc_register_reset);
    EXPECT_EQ(cfg.reconvergence_depth, 12);
    EXPECT_TRUE(cfg.suppress_reset_crossings == false);
    // Strict disables annotation by setting severity of CDC007 to error
    EXPECT_EQ(cfg.rules["CDC007"].severity, "error");
}

TEST(ProfileTest, StringOverload) {
    auto s = get_profile_settings("strict");
    EXPECT_EQ(s.profile, MethodologyProfile::Strict);
}

TEST(ProfileTest, ApplyProfileWiresAllFields) {
    Config cfg;
    ProfileSettings asic = get_profile_settings(MethodologyProfile::AsicSignoff);
    apply_profile(asic, cfg);

    EXPECT_TRUE(cfg.reset_policy.require_cdc_register_reset);
    EXPECT_TRUE(cfg.reset_policy.check_same_clock_reset_crossings);
    EXPECT_TRUE(cfg.reset_policy.detect_reset_synchronizer);
    EXPECT_EQ(cfg.reconvergence_depth, 16);
    EXPECT_FALSE(cfg.suppress_reset_crossings);
    EXPECT_EQ(cfg.min_sync_stages, 3);
    EXPECT_TRUE(cfg.require_structural_proof);
    EXPECT_FALSE(cfg.allow_user_annotation);
    EXPECT_FALSE(cfg.multicycle_path_policy.suppress_findings);
    EXPECT_EQ(cfg.rules["CDC007"].severity, "error");
    EXPECT_EQ(cfg.rules["CDC003"].severity, "error");
}

TEST(ProfileTest, FpgaProfileMulticycle) {
    Config cfg;
    ProfileSettings fpga = get_profile_settings(MethodologyProfile::Fpga);
    apply_profile(fpga, cfg);

    EXPECT_TRUE(cfg.multicycle_path_policy.suppress_findings);
    ASSERT_EQ(cfg.multicycle_path_policy.suppress_rules.size(), 2u);
    EXPECT_EQ(cfg.multicycle_path_policy.suppress_rules[0], "CDC001");
    EXPECT_EQ(cfg.multicycle_path_policy.suppress_rules[1], "CDC002");
}

TEST(ProfileTest, StrictProfileDisablesAnnotation) {
    Config cfg;
    ProfileSettings strict = get_profile_settings(MethodologyProfile::Strict);
    apply_profile(strict, cfg);

    EXPECT_FALSE(cfg.allow_user_annotation);
    EXPECT_TRUE(cfg.require_structural_proof);
    EXPECT_TRUE(cfg.reset_policy.check_same_clock_reset_crossings);
}

TEST(ProfileTest, IsValidProfile) {
    EXPECT_TRUE(is_valid_profile("default"));
    EXPECT_TRUE(is_valid_profile("strict"));
    EXPECT_TRUE(is_valid_profile("asic_signoff"));
    EXPECT_TRUE(is_valid_profile("fpga"));
    EXPECT_TRUE(is_valid_profile("ip_development"));
    EXPECT_TRUE(is_valid_profile("soc_integration"));
    EXPECT_FALSE(is_valid_profile("unknown"));
    EXPECT_FALSE(is_valid_profile(""));
    EXPECT_FALSE(is_valid_profile("ASIC_SIGNOFF"));
}

TEST(ProfileTest, DefaultProfilePreservesDefaults) {
    Config cfg;
    ProfileSettings def = get_profile_settings(MethodologyProfile::Default);
    apply_profile(def, cfg);

    EXPECT_FALSE(cfg.reset_policy.require_cdc_register_reset);
    EXPECT_FALSE(cfg.reset_policy.check_same_clock_reset_crossings);
    EXPECT_EQ(cfg.min_sync_stages, 2);
    EXPECT_FALSE(cfg.require_structural_proof);
    EXPECT_TRUE(cfg.allow_user_annotation);
    EXPECT_TRUE(cfg.multicycle_path_policy.suppress_findings);
}
