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
    bool asynchronous = false;
};

struct ResetPolicyConfig {
    bool require_cdc_register_reset = false;
    bool check_same_clock_reset_crossings = false;
    bool detect_reset_synchronizer = true;
    // A register with an asynchronous reset feeding a synchronizer chain of
    // another clock is no reset-domain crossing: the reset changes the
    // source asynchronously to the destination clock, as any data change
    // does, and the chain resolves it. Off (default): CDC009 is reported.
    bool synchronized_crossings_safe = false;
};

struct MulticyclePathPolicy {
    bool suppress_findings = true;
    std::vector<std::string> suppress_rules = {"CDC001"};
};

struct BlackBoxConfig {
    std::string module_name;
    std::string vendor;
    bool is_safe_crossing = true;
    bool has_synchronizer = false;
    bool has_gray_encoding = false;
    bool has_async_fifo = false;
    bool has_handshake = false;
};

struct Config {
    std::unordered_map<std::string, RuleConfig> rules;
    std::vector<WaiverConfig> waivers;
    std::vector<FalsePathConfig> false_paths;
    std::vector<ClockGroupConfig> clock_groups;
    OutputConfig output;
    ResetPolicyConfig reset_policy;
    MulticyclePathPolicy multicycle_path_policy;
    std::vector<BlackBoxConfig> blackboxes;
    bool suppress_reset_crossings = false;
    int reconvergence_depth = 8;
    int min_sync_stages = 2;
    bool require_structural_proof = false;
    bool allow_user_annotation = true;
    // Conservative sign-off policies (the strict and asic_signoff profiles
    // set both). muxed_clock_domain: a mux of two clocks is a clock domain
    // of its own, so its registers cross from either clock; off, the mux is
    // represented by its default-path input. report_double_synchronization:
    // one source synchronized by two chains that reconverge is CDC003 (the
    // chains may resolve a transition in different cycles); off, two
    // synchronized single-bit paths are taken as safe.
    bool muxed_clock_domain = false;
    bool report_double_synchronization = false;
    // Input frontend selection: "auto" (default), "rtl", or "netlist".
    // CLI --input-type overrides this.
    std::string input_type = "auto";
    // YAML cell map for the netlist frontend (DFF/Latch/ICG pin mapping).
    std::string cell_map_path;
    // Liberty (.lib) file for the netlist frontend (cell definitions).
    std::string liberty_path;
    // RTL->netlist register name map for the netlist frontend.
    std::string name_map_path;
    // Graph safety limit overrides (0 = frontend defaults).
    size_t max_nodes = 0;
    size_t max_edges = 0;
};

class ConfigParser {
   public:
    Config parse_file(const std::string& path, std::string* error = nullptr) const;
    Config parse_string(const std::string& content, std::string* error = nullptr) const;
};

}  // namespace opencdc::config

#endif  // OPENCDC_CONFIG_CONFIG_H
