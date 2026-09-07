#ifndef OPENCDC_REPORT_SARIF_REPORTER_H
#define OPENCDC_REPORT_SARIF_REPORTER_H

#include <ostream>
#include <string>
#include <vector>

#include "analysis/coverage.h"
#include "analysis/signoff.h"
#include "cdc/crossing.h"

namespace opencdc::report {

class SarifReporter {
   public:
    void report(const std::vector<cdc::Finding>& findings, const analysis::CoverageResult& coverage,
                const analysis::SignoffResult& signoff, std::ostream& os) const;

   private:
    static std::string escape_json(const std::string& s);
    static std::string sarif_level(const std::string& severity, bool waived);
};

}  // namespace opencdc::report

#endif  // OPENCDC_REPORT_SARIF_REPORTER_H
