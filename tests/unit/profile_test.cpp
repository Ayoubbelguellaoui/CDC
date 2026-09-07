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
