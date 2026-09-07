#include "analysis/coverage.h"

namespace opencdc::analysis {

CoverageResult CoverageEngine::compute(const std::vector<cdc::Finding>& findings,
                                       const std::string& analysis_status) const {
    CoverageResult result;

    // Map helpers
    std::map<std::pair<std::string, std::string>, size_t> clock_pair_idx;
    std::map<std::string, size_t> module_idx;
    std::map<std::string, size_t> rule_idx;

    for (const auto& f : findings) {
        auto& c = result.counts;
        c.total++;

        // Severity
        if (f.severity == "error")
            c.errors++;
        else if (f.severity == "warning")
            c.warnings++;
        else
            c.info++;

        // Status
        switch (f.safety_status) {
            case cdc::SafetyStatus::VerifiedSafe:
                c.verified_safe++;
                break;
            case cdc::SafetyStatus::VerifiedUnsafe:
                c.verified_unsafe++;
                break;
            case cdc::SafetyStatus::Ambiguous:
                c.ambiguous++;
                break;
            case cdc::SafetyStatus::Candidate:
                c.candidate++;
                break;
            default:
                break;
        }

        if (f.waived) {
            c.waived++;
        } else {
            if (f.severity == "error")
                c.unwaived_errors++;
            if (f.suppressed_by_false_path) {
                c.suppressed++;
            } else if (f.suppressed_by_multicycle) {
                c.multicycle_suppressed++;
            }
        }

        if (f.rule_id == "CDC010")
            c.truncated++;

        // Clock pair coverage
        if (!f.source_domain.empty() && !f.dest_domain.empty()) {
            auto key = std::make_pair(f.source_domain, f.dest_domain);
            auto it = clock_pair_idx.find(key);
            if (it == clock_pair_idx.end()) {
                it = clock_pair_idx.emplace(key, result.clock_pairs.size()).first;
                ClockPairCoverage cp;
                cp.source_domain = f.source_domain;
                cp.dest_domain = f.dest_domain;
                result.clock_pairs.push_back(std::move(cp));
            }
            auto& cp = result.clock_pairs[it->second];
            cp.crossing_count++;
            if (f.severity == "error")
                cp.error_count++;
            else if (f.severity == "warning")
                cp.warning_count++;
            if (f.safety_status == cdc::SafetyStatus::VerifiedSafe)
                cp.verified_safe++;
            else if (f.safety_status == cdc::SafetyStatus::VerifiedUnsafe)
                cp.verified_unsafe++;
        }

        // Module coverage
        if (!f.source_module_path.empty()) {
            auto it = module_idx.find(f.source_module_path);
            if (it == module_idx.end()) {
                it = module_idx.emplace(f.source_module_path, result.modules.size()).first;
                ModuleCoverage mc;
                mc.module_path = f.source_module_path;
                result.modules.push_back(std::move(mc));
            }
            auto& mc = result.modules[it->second];
            mc.crossing_count++;
            if (f.severity == "error")
                mc.error_count++;
            else if (f.severity == "warning")
                mc.warning_count++;
        }

        // Rule coverage
        if (!f.rule_id.empty()) {
            auto it = rule_idx.find(f.rule_id);
            if (it == rule_idx.end()) {
                it = rule_idx.emplace(f.rule_id, result.rules.size()).first;
                RuleCoverage rc;
                rc.rule_id = f.rule_id;
                result.rules.push_back(std::move(rc));
            }
            auto& rc = result.rules[it->second];
            rc.count++;
            if (f.waived)
                rc.waived++;
        }
    }

    result.counts.analyzed = result.counts.total - result.counts.truncated;

    return result;
}

}  // namespace opencdc::analysis
