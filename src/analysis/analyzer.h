#ifndef OPENCDC_ANALYSIS_ANALYZER_H
#define OPENCDC_ANALYSIS_ANALYZER_H

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cdc/crossing.h"
#include "clock/domain.h"
#include "config/config.h"
#include "ir/graph.h"

namespace opencdc::analysis {

struct AnalysisRequest {
    std::vector<std::string> input_files;
    std::string top_module;
    std::string config_path;
    std::string waiver_path;
    std::string constraints_path;
    // Explicit false paths as (source, destination) register name pairs.
    std::vector<std::pair<std::string, std::string>> false_paths;
    std::vector<std::string> disable_rules;
    std::vector<std::string> severity_overrides;  // "RULE=severity"
    // Pre-parsed config (optional). When set, the analyzer skips re-parsing.
    std::optional<config::Config> config;
};

struct AnalysisResult {
    bool ok = false;
    std::string analysis_status = "complete";
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    ir::Graph graph;
    clock::DomainResult domains;
    std::vector<cdc::Finding> findings;
};

// Runs the full CDC analysis pipeline. Single entry point shared by the
// CLI, LSP server, and Python bindings so behavior cannot drift.
class Analyzer {
   public:
    AnalysisResult run(const AnalysisRequest& request);
};

}  // namespace opencdc::analysis

#endif  // OPENCDC_ANALYSIS_ANALYZER_H
