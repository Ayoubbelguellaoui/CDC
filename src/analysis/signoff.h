#ifndef OPENCDC_ANALYSIS_SIGNOFF_H
#define OPENCDC_ANALYSIS_SIGNOFF_H

#include <string>
#include <vector>

#include "analysis/coverage.h"
#include "cdc/crossing.h"

namespace opencdc::analysis {

enum class SignoffStatus { Pass, PassWithWaivers, Fail, Incomplete, Error };

std::string signoff_status_name(SignoffStatus s);

struct SignoffResult {
    SignoffStatus status = SignoffStatus::Error;
    std::string reason;
    CoverageResult coverage;
};

class SignoffEngine {
   public:
    SignoffResult evaluate(const std::vector<cdc::Finding>& findings,
                           const std::string& analysis_status) const;
};

}  // namespace opencdc::analysis

#endif  // OPENCDC_ANALYSIS_SIGNOFF_H
