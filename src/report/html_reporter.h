#ifndef OPENCDC_REPORT_HTML_REPORTER_H
#define OPENCDC_REPORT_HTML_REPORTER_H

#include <fstream>
#include <string>
#include <vector>

#include "cdc/crossing.h"

namespace opencdc::report {

struct HtmlReportOptions {
    std::string output_dir = "opencdc_report";
    std::string title = "OpenCDC Analysis Report";
    bool include_source_snippets = true;
    bool include_summary_dashboard = true;
    bool dark_mode = false;
    std::string custom_css;
};

class HtmlReporter {
   public:
    void generate_report(const std::vector<cdc::Finding>& findings,
                         const HtmlReportOptions& options = {},
                         const std::string& analysis_status = "complete");

   private:
    void write_index_html(const std::vector<cdc::Finding>& findings,
                          const HtmlReportOptions& options);
    void write_findings_html(const std::vector<cdc::Finding>& findings,
                             const HtmlReportOptions& options);
    void write_summary_html(const std::vector<cdc::Finding>& findings,
                            const HtmlReportOptions& options);
    void write_css(const HtmlReportOptions& options);
    void write_js(const HtmlReportOptions& options);

    std::string generate_summary_dashboard(const std::vector<cdc::Finding>& findings,
                                           const std::string& analysis_status = "complete");
    std::string generate_findings_table(const std::vector<cdc::Finding>& findings,
                                        bool include_source_snippets = true);
    std::string generate_severity_chart(const std::vector<cdc::Finding>& findings);
    std::string generate_rule_chart(const std::vector<cdc::Finding>& findings);

    std::string escape_html(const std::string& s);
    std::string severity_class(const std::string& severity);
    std::string severity_icon(const std::string& severity);

    void ensure_directory_exists(const std::string& path);
};

}  // namespace opencdc::report

#endif  // OPENCDC_REPORT_HTML_REPORTER_H
