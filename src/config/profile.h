#ifndef OPENCDC_CONFIG_PROFILE_H
#define OPENCDC_CONFIG_PROFILE_H

#include <string>
#include <vector>

#include "config/config.h"

namespace opencdc::config {

enum class MethodologyProfile { Default, Strict, AsicSignoff, Fpga, IpDevelopment, SocIntegration };

std::string profile_name(MethodologyProfile p);
MethodologyProfile profile_from_name(const std::string& name);

struct ProfileSettings {
    MethodologyProfile profile = MethodologyProfile::Default;
    std::string description;

    // Reset policy
    bool require_cdc_register_reset = false;
    bool require_reset_synchronizer = false;

    // Annotation policy
    bool allow_annotation = true;
    bool require_structural_proof = false;

    // Synchronizer requirements
    int min_sync_stages = 2;

    // Analysis depth
    int reconvergence_depth = 8;

    // Suppression
    bool suppress_reset_crossings = false;

    // Severity overrides: rule_id -> severity
    std::vector<std::pair<std::string, std::string>> severity_overrides;

    // Disabled rules
    std::vector<std::string> disabled_rules;
};

ProfileSettings get_profile_settings(MethodologyProfile profile);
ProfileSettings get_profile_settings(const std::string& profile_name);

void apply_profile(ProfileSettings& settings, Config& cfg);

}  // namespace opencdc::config

#endif  // OPENCDC_CONFIG_PROFILE_H
