#ifndef OPENCDC_CONFIG_CONFIG_H
#define OPENCDC_CONFIG_CONFIG_H

#include <string>
#include <unordered_map>
#include <vector>

namespace opencdc::config {

struct RuleConfig {
    bool enabled = true;
    std::string severity;
};

struct WaiverConfig {
    std::string rule_id;
    std::string source_reg;
    std::string dest_reg;
    std::string source_domain;
    std::string dest_domain;
    std::string justification;
    std::string owner;
    std::string expiry;
};

struct OutputConfig {
    std::string format;
    std::string file;
};

struct FalsePathConfig {
    std::string source_reg;
    std::string dest_reg;
    std::string source_clock;
    std::string dest_clock;
};

struct ClockGroupConfig {
    std::vector<std::string> clocks;
    bool exclusive = false;
};

struct ResetPolicyConfig {
    bool require_cdc_register_reset = false;
    bool check_same_clock_reset_crossings = false;
    bool detect_reset_synchronizer = true;
};

struct MulticyclePathPolicy {
    bool suppress_findings = true;
    std::vector<std::string> suppress_rules = {"CDC001"};
};

struct Config {
    std::unordered_map<std::string, RuleConfig> rules;
    std::vector<WaiverConfig> waivers;
    std::vector<FalsePathConfig> false_paths;
    std::vector<ClockGroupConfig> clock_groups;
    OutputConfig output;
    ResetPolicyConfig reset_policy;
    MulticyclePathPolicy multicycle_path_policy;
    bool suppress_reset_crossings = false;
    int reconvergence_depth = 8;
};

class ConfigParser {
   public:
    Config parse_file(const std::string& path, std::string* error = nullptr) const;
    Config parse_string(const std::string& content, std::string* error = nullptr) const;

   private:
    void parse_rule_section(const std::string& content, Config& config) const;
    void parse_waiver_section(const std::string& content, Config& config) const;
    void parse_output_section(const std::string& content, Config& config) const;
    void parse_false_path_section(const std::string& content, Config& config) const;
    void parse_reset_policy_section(const std::string& content, Config& config) const;
    void parse_multicycle_policy_section(const std::string& content, Config& config) const;
    static std::string trim(const std::string& s);
    static std::string to_lower(const std::string& s);
};

}  // namespace opencdc::config

#endif  // OPENCDC_CONFIG_CONFIG_H
