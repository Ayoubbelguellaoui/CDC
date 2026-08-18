#ifndef OPENCDC_REPORT_REPORT_H
#define OPENCDC_REPORT_REPORT_H

#include "cdc/crossing.h"
#include <ostream>
#include <string>
#include <vector>

namespace opencdc::report {

struct ReportCounts {
    size_t total = 0;
    size_t errors = 0;
    size_t warnings = 0;
    size_t waived = 0;
};

class Reporter {
public:
    void report_json(const std::vector<cdc::Finding>& findings, std::ostream& os) const;
    void report_text(const std::vector<cdc::Finding>& findings, std::ostream& os) const;
    void report_summary(const std::vector<cdc::Finding>& findings, std::ostream& os) const;

    ReportCounts count(const std::vector<cdc::Finding>& findings) const;

    static std::string escape_json(const std::string& s);

    bool has_unsuppressed_errors(const std::vector<cdc::Finding>& findings) const;
};

} // namespace opencdc::report

#endif // OPENCDC_REPORT_REPORT_H
