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

struct Config {
    std::unordered_map<std::string, RuleConfig> rules;
    std::vector<WaiverConfig> waivers;
    std::vector<FalsePathConfig> false_paths;
    std::vector<ClockGroupConfig> clock_groups;
    OutputConfig output;
    bool suppress_reset_crossings = false;
};

class ConfigParser {
   public:
    Config parse_file(const std::string& path, std::string* error = nullptr) const;
    Config parse_string(const std::string& content, std::string* error = nullptr) const;
};

}  // namespace opencdc::config

#endif  // OPENCDC_CONFIG_CONFIG_H
