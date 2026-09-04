#include "report/report.h"

#include <algorithm>

namespace opencdc::report {

std::string Reporter::escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

ReportCounts Reporter::count(const std::vector<cdc::Finding>& findings) const {
    ReportCounts c;
    c.total = findings.size();
    for (const auto& f : findings) {
        if (f.waived) {
            c.waived++;
            continue;
        }
        if (f.severity == "error")
            c.errors++;
        else if (f.severity == "warning")
            c.warnings++;
    }
    return c;
}

bool Reporter::has_unsuppressed_errors(const std::vector<cdc::Finding>& findings) const {
    for (const auto& f : findings) {
        if (!f.waived && f.severity == "error")
            return true;
    }
    return false;
}

void Reporter::report_json(const std::vector<cdc::Finding>& findings, std::ostream& os) const {
    auto sorted = sorted_findings(findings);
    os << "[\n";
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& f = sorted[i];
        os << "  {\n"
           << "    \"rule_id\": \"" << escape_json(f.rule_id) << "\",\n"
           << "    \"rule_name\": \"" << escape_json(f.rule_name) << "\",\n"
           << "    \"severity\": \"" << escape_json(f.severity) << "\",\n"
           << "    \"waived\": " << (f.waived ? "true" : "false") << ",\n";

        if (f.waived) {
            os << "    \"waiver_justification\": \"" << escape_json(f.waiver_justification)
               << "\",\n"
               << "    \"waiver_owner\": \"" << escape_json(f.waiver_owner) << "\",\n";
        }

        os << "    \"source\": \"" << escape_json(f.source_reg_name) << "\",\n"
           << "    \"source_domain\": \"" << escape_json(f.source_domain) << "\",\n"
           << "    \"dest\": \"" << escape_json(f.dest_reg_name) << "\",\n"
           << "    \"dest_domain\": \"" << escape_json(f.dest_domain) << "\",\n"
           << "    \"bus_width\": " << f.bus_width << ",\n"
           << "    \"reason\": \"" << escape_json(f.reason) << "\",\n";

        if (f.has_multicycle_exception) {
            os << "    \"multicycle_cycles\": " << f.multicycle_cycles << ",\n"
               << "    \"constraint_source\": \"" << escape_json(f.constraint_source) << "\",\n";
        }

        os << "    \"file\": \"" << escape_json(f.source_loc.file) << "\",\n"
           << "    \"line\": " << f.source_loc.line << "\n"
           << "  }";

        if (i + 1 < sorted.size())
            os << ",";
        os << "\n";
    }
    os << "]\n";
}

void Reporter::report_text(const std::vector<cdc::Finding>& findings, std::ostream& os) const {
    auto sorted = sorted_findings(findings);
    for (const auto& f : sorted) {
        // clang-format off
        os << f.severity << " [" << f.rule_id << "] " << f.source_reg_name << " ("
           << f.source_domain << ")"
           << " -> " << f.dest_reg_name << " (" << f.dest_domain << ")";
        // clang-format on

        if (f.waived)
            os << " [WAIVED]";
        if (f.has_multicycle_exception)
            os << " [MC:" << f.multicycle_cycles << "x]";

        os << "\n  " << f.reason << "\n";

        if (!f.source_loc.file.empty()) {
            os << "  at " << f.source_loc.file << ":" << f.source_loc.line << "\n";
        }

        if (f.waived && !f.waiver_justification.empty()) {
            os << "  waiver: " << f.waiver_justification;
            if (!f.waiver_owner.empty())
                os << " (" << f.waiver_owner << ")";
            os << "\n";
        }
    }
}

void Reporter::report_summary(const std::vector<cdc::Finding>& findings, std::ostream& os) const {
    auto c = count(findings);
    if (c.total == 0) {
        os << "No CDC findings.\n";
        return;
    }
    os << c.total << " finding(s)";
    if (c.errors > 0)
        os << ", " << c.errors << " error(s)";
    if (c.warnings > 0)
        os << ", " << c.warnings << " warning(s)";
    if (c.waived > 0)
        os << ", " << c.waived << " waived";
    os << "\n";
}

std::vector<cdc::Finding> Reporter::sorted_findings(const std::vector<cdc::Finding>& findings) {
    std::vector<cdc::Finding> sorted = findings;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const cdc::Finding& a, const cdc::Finding& b) {
                         if (a.source_loc.file != b.source_loc.file)
                             return a.source_loc.file < b.source_loc.file;
                         if (a.source_loc.line != b.source_loc.line)
                             return a.source_loc.line < b.source_loc.line;
                         if (a.rule_id != b.rule_id)
                             return a.rule_id < b.rule_id;
                         if (a.source_reg_name != b.source_reg_name)
                             return a.source_reg_name < b.source_reg_name;
                         return a.dest_reg_name < b.dest_reg_name;
                     });
    return sorted;
}

}  // namespace opencdc::report
