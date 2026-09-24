#include "report/sarif_reporter.h"

#include <algorithm>
#include <cctype>

#include "analysis/signoff.h"
#include "clock/relationship.h"
#include "opencdc/version.h"
#include "report/report.h"

namespace opencdc::report {

static const char* safety_status_str(cdc::SafetyStatus s) {
    switch (s) {
        case cdc::SafetyStatus::Candidate:
            return "candidate";
        case cdc::SafetyStatus::VerifiedSafe:
            return "verified_safe";
        case cdc::SafetyStatus::VerifiedUnsafe:
            return "verified_unsafe";
        case cdc::SafetyStatus::Ambiguous:
            return "ambiguous";
        default:
            return "unknown";
    }
}

static std::string sarif_escape(const std::string& s) {
    return Reporter::escape_json(s);
}

// SARIF artifactLocation.uri must be a URI, not a raw filesystem path:
// encode spaces/control/special chars, and prefix absolute paths with
// the file:// scheme. Relative paths are emitted as relative URIs.
static std::string sarif_uri(const std::string& path) {
    if (path.empty())
        return "";
    auto needs_encode = [](char c) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            return false;
        switch (c) {
            case '-':
            case '_':
            case '.':
            case '~':
            case '/':
            case ':':
                return false;
            default:
                return true;
        }
    };
    std::string out;
    if (!path.empty() && path[0] == '/')
        out += "file://";
    static const char* hex = "0123456789ABCDEF";
    for (char c : path) {
        if (needs_encode(c)) {
            unsigned char u = static_cast<unsigned char>(c);
            out += '%';
            out += hex[(u >> 4) & 0xF];
            out += hex[u & 0xF];
        } else {
            out += c;
        }
    }
    return out;
}

std::string SarifReporter::sarif_level(const std::string& severity, bool waived) {
    if (waived)
        return "note";
    if (severity == "error")
        return "error";
    if (severity == "warning")
        return "warning";
    return "note";
}

void SarifReporter::report(const std::vector<cdc::Finding>& findings,
                           const analysis::CoverageResult& coverage,
                           const analysis::SignoffResult& signoff, std::ostream& os) const {
    auto sorted = Reporter::sorted_findings(findings);

    os << "{\n"
       << "  \"$schema\": "
          "\"https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/"
          "sarif-schema-2.1.0.json\",\n"
       << "  \"version\": \"2.1.0\",\n"
       << "  \"runs\": [\n"
       << "    {\n"
       << "      \"tool\": {\n"
       << "        \"driver\": {\n"
       << "          \"name\": \"opencdc\",\n"
       << "          \"version\": \"" << OPENCDC_VERSION << "\",\n"
       << "          \"informationUri\": \"https://github.com/opencdc/opencdc\",\n"
       << "          \"rules\": [\n";

    // Emit one rule descriptor per unique rule_id
    std::vector<std::string> seen_rules;
    for (const auto& f : sorted) {
        bool found = false;
        for (const auto& r : seen_rules) {
            if (r == f.rule_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            seen_rules.push_back(f.rule_id);
        }
    }

    for (size_t i = 0; i < seen_rules.size(); ++i) {
        const auto& rule_id = seen_rules[i];
        os << "            {\n"
           << "              \"id\": \"" << sarif_escape(rule_id) << "\",\n"
           << "              \"name\": \"" << sarif_escape(rule_id) << "\",\n"
           << "              \"shortDescription\": {\n"
           << "                \"text\": \"CDC rule " << sarif_escape(rule_id) << "\"\n"
           << "              }\n"
           << "            }";
        if (i + 1 < seen_rules.size())
            os << ",";
        os << "\n";
    }

    os << "          ]\n"
       << "        }\n"
       << "      },\n"
       << "      \"results\": [\n";

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& f = sorted[i];
        os << "        {\n"
           << "          \"ruleId\": \"" << sarif_escape(f.rule_id) << "\",\n"
           << "          \"level\": \"" << sarif_level(f.severity, f.waived) << "\",\n"
           << "          \"message\": {\n"
           << "            \"text\": \"" << sarif_escape(f.reason) << "\"\n"
           << "          },\n"
           << "          \"locations\": [\n"
           << "            {\n"
           << "              \"physicalLocation\": {\n"
           << "                \"artifactLocation\": {\n"
           << "                  \"uri\": \"" << sarif_escape(sarif_uri(f.source_loc.file))
           << "\"\n"
           << "                },\n"
           << "                \"region\": {\n"
           << "                  \"startLine\": " << (f.source_loc.line > 0 ? f.source_loc.line : 1)
           << "\n"
           << "                }\n"
           << "              }\n"
           << "            }\n"
           << "          ],\n"
           << "          \"properties\": {\n"
           << "            \"source_domain\": \"" << sarif_escape(f.source_domain) << "\",\n"
           << "            \"dest_domain\": \"" << sarif_escape(f.dest_domain) << "\",\n"
           << "            \"source_reg\": \"" << sarif_escape(f.source_reg_name) << "\",\n"
           << "            \"dest_reg\": \"" << sarif_escape(f.dest_reg_name) << "\",\n"
           << "            \"bus_width\": " << f.bus_width << ",\n"
           << "            \"safety_status\": \"" << safety_status_str(f.safety_status) << "\"\n"
           << "          }\n"
           << "        }";

        if (i + 1 < sorted.size())
            os << ",";
        os << "\n";
    }

    os << "      ]\n"
       << "      ,\n"
       << "      \"properties\": {\n"
       << "        \"methodology\": \"" << sarif_escape(signoff.methodology) << "\",\n"
       << "        \"signoff\": \"" << sarif_escape(analysis::signoff_status_name(signoff.status))
       << "\",\n"
       << "        \"signoff_reason\": \"" << sarif_escape(signoff.reason) << "\",\n"
       << "        \"total_findings\": " << sorted.size() << ",\n"
       << "        \"total_crossings\": " << coverage.counts.total_crossings << ",\n"
       << "        \"analyzed_crossings\": " << coverage.counts.analyzed_crossings << ",\n"
       << "        \"unwaived_errors\": " << coverage.counts.unwaived_errors << ",\n"
       << "        \"waived\": " << coverage.counts.waived << "\n"
       << "      }\n"
       << "    }\n"
       << "  ]\n"
       << "}\n";
}

}  // namespace opencdc::report
