#ifndef OPENCDC_ANALYSIS_COVERAGE_H
#define OPENCDC_ANALYSIS_COVERAGE_H

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "cdc/crossing.h"
#include "clock/domain.h"
#include "ir/graph.h"

namespace opencdc::analysis {

struct CoverageCounts {
    size_t total = 0;
    size_t analyzed = 0;
    size_t truncated = 0;
    size_t errors = 0;
    size_t warnings = 0;
    size_t info = 0;
    size_t unwaived_errors = 0;
    size_t verified_safe = 0;
    size_t verified_unsafe = 0;
    size_t ambiguous = 0;
    size_t candidate = 0;
    size_t waived = 0;
    size_t suppressed = 0;
    size_t multicycle_suppressed = 0;
    // Crossing-level coverage (design-wide metrics)
    size_t total_crossings = 0;
    size_t analyzed_crossings = 0;
    size_t suppressed_crossings = 0;
    size_t skipped_same_domain = 0;
    size_t skipped_no_domain = 0;
};

struct ClockPairCoverage {
    std::string source_domain;
    std::string dest_domain;
    size_t crossing_count = 0;
    size_t error_count = 0;
    size_t warning_count = 0;
    size_t verified_safe = 0;
    size_t verified_unsafe = 0;
};

struct ModuleCoverage {
    std::string module_path;
    size_t crossing_count = 0;
    size_t error_count = 0;
    size_t warning_count = 0;
};

struct RuleCoverage {
    std::string rule_id;
    size_t count = 0;
    size_t waived = 0;
};

struct CoverageResult {
    CoverageCounts counts;
    std::vector<ClockPairCoverage> clock_pairs;
    std::vector<ModuleCoverage> modules;
    std::vector<RuleCoverage> rules;
};

class CoverageEngine {
   public:
    CoverageResult compute(const std::vector<cdc::Finding>& findings,
                           const std::string& analysis_status) const;

    void compute_crossing_coverage(
        CoverageResult& result, const ir::Graph& graph,
        const std::vector<clock::ClockDomain>& domains,
        const std::unordered_map<uint64_t, size_t>& register_to_domain) const;
};

}  // namespace opencdc::analysis

#endif  // OPENCDC_ANALYSIS_COVERAGE_H
