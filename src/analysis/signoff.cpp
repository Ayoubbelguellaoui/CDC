#include "analysis/signoff.h"

#include <algorithm>

namespace opencdc::analysis {

std::string signoff_status_name(SignoffStatus s) {
    switch (s) {
        case SignoffStatus::Pass:
            return "PASS";
        case SignoffStatus::PassWithWaivers:
            return "PASS_WITH_WAIVERS";
        case SignoffStatus::Fail:
            return "FAIL";
        case SignoffStatus::Incomplete:
            return "INCOMPLETE";
        case SignoffStatus::Error:
            return "ERROR";
    }
    return "ERROR";
}

SignoffResult SignoffEngine::evaluate(const std::vector<cdc::Finding>& findings,
                                      const std::string& analysis_status,
                                      const config::Config& cfg,
                                      const std::string& methodology) const {
    SignoffResult result;
    result.methodology = methodology;

    CoverageEngine coverage_engine;
    result.coverage = coverage_engine.compute(findings, analysis_status);

    const auto& c = result.coverage.counts;

    if (analysis_status == "failed") {
        result.status = SignoffStatus::Error;
        result.reason = "Analysis failed";
        return result;
    }

    if (analysis_status == "incomplete") {
        result.status = SignoffStatus::Incomplete;
        result.reason = "Analysis incomplete — path traversal truncated";
        return result;
    }

    // Methodology-specific warnings-as-errors logic.
    bool strict_warnings = (methodology == "asic_signoff" || methodology == "strict");
    if (strict_warnings) {
        // For ASIC/strict: warnings on critical rules are fatal.
        static const std::vector<std::string> critical_rules = {"CDC001", "CDC002", "CDC004",
                                                               "CDC005", "CDC007"};
        for (const auto& f : findings) {
            if (f.severity == "warning" && !f.waived) {
                bool is_critical = std::find(critical_rules.begin(), critical_rules.end(),
                                             f.rule_id) != critical_rules.end();
                if (is_critical)
                    result.warnings_as_errors++;
            }
        }
    }

    // Fail: unsuppressed errors exist.
    if (c.unwaived_errors > 0) {
        result.status = SignoffStatus::Fail;
        result.reason = std::to_string(c.unwaived_errors) + " unsuppressed error(s) found";
        return result;
    }

    // Fail: warnings-as-errors in strict methodology.
    if (result.warnings_as_errors > 0) {
        result.status = SignoffStatus::Fail;
        result.reason = std::to_string(result.warnings_as_errors) +
                        " warning(s) treated as error(s) under " + methodology + " methodology";
        return result;
    }

    if (c.waived > 0) {
        result.status = SignoffStatus::PassWithWaivers;
        result.reason = "No unsuppressed errors — " + std::to_string(c.waived) + " waived";
        return result;
    }

    result.status = SignoffStatus::Pass;
    result.reason = "No CDC violations found";
    return result;
}

}  // namespace opencdc::analysis
