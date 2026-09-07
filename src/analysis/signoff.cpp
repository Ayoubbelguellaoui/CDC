#include "analysis/signoff.h"

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
                                      const std::string& analysis_status) const {
    SignoffResult result;

    CoverageEngine coverage_engine;
    result.coverage = coverage_engine.compute(findings, analysis_status);

    const auto& c = result.coverage.counts;

    // Error: analysis failed entirely.
    if (analysis_status == "failed") {
        result.status = SignoffStatus::Error;
        result.reason = "Analysis failed";
        return result;
    }

    // Incomplete: path traversal truncated, some crossings may be missed.
    if (analysis_status == "incomplete") {
        result.status = SignoffStatus::Incomplete;
        result.reason = "Analysis incomplete — path traversal truncated";
        return result;
    }

    // Fail: unsuppressed errors exist.
    if (c.unwaived_errors > 0) {
        result.status = SignoffStatus::Fail;
        result.reason = std::to_string(c.unwaived_errors) + " unsuppressed error(s) found";
        return result;
    }

    // Pass with waivers: no errors, but some findings were waived.
    if (c.waived > 0) {
        result.status = SignoffStatus::PassWithWaivers;
        result.reason = "No unsuppressed errors — " + std::to_string(c.waived) + " waived";
        return result;
    }

    // Clean pass.
    result.status = SignoffStatus::Pass;
    result.reason = "No CDC violations found";
    return result;
}

}  // namespace opencdc::analysis
