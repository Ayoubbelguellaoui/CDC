#ifndef OPENCDC_OPENCDC_H
#define OPENCDC_OPENCDC_H

#include <cstdlib>
#include <string>
#include <vector>

namespace opencdc {

struct CheckOptions {
    std::string top_module;
    std::vector<std::string> input_files;
    std::string config_path;
    std::string output_path;
    std::string format = "json";
    bool format_explicit = false;
    std::string waiver_path;
    std::string constraints_path;
    std::string html_output_dir;
    bool verbose = false;
    size_t num_threads = 0;
    std::string save_baseline;
    // Register inventory reports (report/domain_report.h); empty: none.
    std::string clock_report;
    std::string reset_report;
    std::string compare_baseline;
    std::vector<std::string> disable_rules;
    std::vector<std::string> severity_overrides;
    std::vector<std::pair<std::string, std::string>> false_paths;
    bool signoff_mode = false;
    std::string profile;
    std::vector<std::string> include_dirs;
    std::vector<std::string> defines;
    // Language frontend: "auto" (default), "rtl", or "netlist".
    std::string input_type = "auto";
    bool input_type_explicit = false;
    // YAML cell map for the netlist frontend.
    std::string cell_map_path;
    // Liberty (.lib) file for the netlist frontend.
    std::string liberty_path;
    // RTL->netlist register name map for the netlist frontend.
    std::string name_map_path;
    // Graph safety limit overrides (0 = frontend defaults).
    size_t max_nodes = 0;
    size_t max_edges = 0;
    // Text-report colorization: "auto" (default), "always", or "never".
    std::string color_mode = "auto";
};

enum class ExitCode : int {
    OK = 0,
    FINDINGS = 1,
    INPUT_ERROR = 2,
    INTERNAL_ERROR = 3,
};

int run(int argc, const char* argv[]);

}  // namespace opencdc

#endif  // OPENCDC_OPENCDC_H
