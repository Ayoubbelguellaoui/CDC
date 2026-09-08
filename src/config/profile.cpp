#include "config/profile.h"

namespace opencdc::config {

std::string profile_name(MethodologyProfile p) {
    switch (p) {
        case MethodologyProfile::Default:
            return "default";
        case MethodologyProfile::Strict:
            return "strict";
        case MethodologyProfile::AsicSignoff:
            return "asic_signoff";
        case MethodologyProfile::Fpga:
            return "fpga";
        case MethodologyProfile::IpDevelopment:
            return "ip_development";
        case MethodologyProfile::SocIntegration:
            return "soc_integration";
    }
    return "default";
}

MethodologyProfile profile_from_name(const std::string& name) {
    if (name == "strict")
        return MethodologyProfile::Strict;
    if (name == "asic_signoff")
        return MethodologyProfile::AsicSignoff;
    if (name == "fpga")
        return MethodologyProfile::Fpga;
    if (name == "ip_development")
        return MethodologyProfile::IpDevelopment;
    if (name == "soc_integration")
        return MethodologyProfile::SocIntegration;
    return MethodologyProfile::Default;
}

ProfileSettings get_profile_settings(MethodologyProfile profile) {
    ProfileSettings s;

    switch (profile) {
        case MethodologyProfile::Default:
            s.profile = MethodologyProfile::Default;
            s.description = "Default settings: balanced analysis";
            s.require_cdc_register_reset = false;
            s.require_reset_synchronizer = false;
            s.allow_annotation = true;
            s.require_structural_proof = false;
            s.min_sync_stages = 2;
            s.reconvergence_depth = 8;
            s.suppress_reset_crossings = false;
            break;

        case MethodologyProfile::Strict:
            s.profile = MethodologyProfile::Strict;
            s.description =
                "Strict: require all CDC registers to have reset, "
                "structural proof for safety classifications";
            s.require_cdc_register_reset = true;
            s.require_reset_synchronizer = true;
            s.check_same_clock_reset_crossings = true;
            s.allow_annotation = false;
            s.require_structural_proof = true;
            s.min_sync_stages = 3;
            s.reconvergence_depth = 12;
            s.suppress_reset_crossings = false;
            s.multicycle_suppress_findings = false;
            s.severity_overrides.push_back({"CDC007", "error"});
            break;

        case MethodologyProfile::AsicSignoff:
            s.profile = MethodologyProfile::AsicSignoff;
            s.description = "ASIC signoff: maximum rigor for tapeout";
            s.require_cdc_register_reset = true;
            s.require_reset_synchronizer = true;
            s.check_same_clock_reset_crossings = true;
            s.allow_annotation = false;
            s.require_structural_proof = true;
            s.min_sync_stages = 3;
            s.reconvergence_depth = 16;
            s.suppress_reset_crossings = false;
            s.multicycle_suppress_findings = false;
            s.severity_overrides.push_back({"CDC007", "error"});
            s.severity_overrides.push_back({"CDC003", "error"});
            break;

        case MethodologyProfile::Fpga:
            s.profile = MethodologyProfile::Fpga;
            s.description = "FPGA: relaxed reset, FPGA-friendly settings";
            s.require_cdc_register_reset = false;
            s.require_reset_synchronizer = false;
            s.allow_annotation = true;
            s.require_structural_proof = false;
            s.min_sync_stages = 2;
            s.reconvergence_depth = 8;
            s.suppress_reset_crossings = true;
            s.multicycle_suppress_findings = true;
            s.multicycle_suppress_rules = {"CDC001", "CDC002"};
            s.disabled_rules.push_back("CDC009");
            break;

        case MethodologyProfile::IpDevelopment:
            s.profile = MethodologyProfile::IpDevelopment;
            s.description = "IP development: focus on crossings, less on reset";
            s.require_cdc_register_reset = false;
            s.require_reset_synchronizer = false;
            s.allow_annotation = true;
            s.require_structural_proof = false;
            s.min_sync_stages = 2;
            s.reconvergence_depth = 8;
            s.suppress_reset_crossings = true;
            s.disabled_rules.push_back("CDC009");
            s.disabled_rules.push_back("CDC007");
            break;

        case MethodologyProfile::SocIntegration:
            s.profile = MethodologyProfile::SocIntegration;
            s.description = "SoC integration: focus on inter-module crossings";
            s.require_cdc_register_reset = false;
            s.require_reset_synchronizer = false;
            s.allow_annotation = true;
            s.require_structural_proof = false;
            s.min_sync_stages = 2;
            s.reconvergence_depth = 10;
            s.suppress_reset_crossings = false;
            break;
    }

    return s;
}

ProfileSettings get_profile_settings(const std::string& name) {
    return get_profile_settings(profile_from_name(name));
}

void apply_profile(ProfileSettings& settings, Config& cfg) {
    cfg.reset_policy.require_cdc_register_reset = settings.require_cdc_register_reset;
    cfg.reset_policy.detect_reset_synchronizer = settings.require_reset_synchronizer;
    cfg.reset_policy.check_same_clock_reset_crossings = settings.check_same_clock_reset_crossings;
    cfg.reconvergence_depth = settings.reconvergence_depth;
    cfg.suppress_reset_crossings = settings.suppress_reset_crossings;
    cfg.min_sync_stages = settings.min_sync_stages;
    cfg.require_structural_proof = settings.require_structural_proof;
    cfg.allow_user_annotation = settings.allow_annotation;

    cfg.multicycle_path_policy.suppress_findings = settings.multicycle_suppress_findings;
    cfg.multicycle_path_policy.suppress_rules = settings.multicycle_suppress_rules;

    for (const auto& [rule_id, severity] : settings.severity_overrides) {
        cfg.rules[rule_id].severity = severity;
    }

    for (const auto& rule_id : settings.disabled_rules) {
        cfg.rules[rule_id].enabled = false;
    }
}

bool is_valid_profile(const std::string& name) {
    return name == "default" || name == "strict" || name == "asic_signoff" || name == "fpga" ||
           name == "ip_development" || name == "soc_integration";
}

}  // namespace opencdc::config
