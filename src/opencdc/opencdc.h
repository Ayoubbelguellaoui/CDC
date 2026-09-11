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
    std::string waiver_path;
    std::string constraints_path;
    std::string html_output_dir;
    bool verbose = false;
    bool signoff_mode = false;
    std::vector<std::string> disable_rules;
    std::vector<std::string> severity_overrides;
    std::vector<std::pair<std::string, std::string>> false_paths;
    std::string save_baseline;
    std::string compare_baseline;
    std::string profile;
    std::vector<std::string> include_dirs;
    std::vector<std::string> defines;
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
