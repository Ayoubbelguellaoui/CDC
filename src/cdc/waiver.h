#ifndef OPENCDC_CDC_WAIVER_H
#define OPENCDC_CDC_WAIVER_H

#include "cdc/crossing.h"
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace opencdc::cdc {

enum class WaiverMatchType {
    Substring,
    Wildcard,
    Regex
};

struct Waiver {
    std::string rule_id;
    std::string source_reg_name;
    std::string dest_reg_name;
    std::string source_domain;
    std::string dest_domain;
    std::string justification;
    std::string owner;
    std::string expiry;
    WaiverMatchType match_type = WaiverMatchType::Substring;

    // Pre-compiled regex (populated by WaiverEngine::add_waiver for Regex type).
    // Using shared_ptr so Waiver remains copyable.
    std::shared_ptr<std::regex> source_regex;
    std::shared_ptr<std::regex> dest_regex;
};

class WaiverEngine {
public:
    // Loads waivers from a whitespace-separated waiver file. On failure returns
    // false and (if provided) sets *error to a diagnostic. A file that parses
    // but yields zero valid waivers is treated as a failure.
    bool load_from_file(const std::string& path, std::string* error = nullptr);
    // Returns false (and does not store the waiver) when a Wildcard/Regex
    // waiver fails to compile, so callers can surface the drop.
    bool add_waiver(const Waiver& w);

    std::vector<Finding> apply(const std::vector<Finding>& findings) const;
    bool matches(const Finding& f, const Waiver& w) const;

    const std::vector<Waiver>& waivers() const { return waivers_; }

    std::vector<std::string> check_unused(const std::vector<Finding>& findings) const;

    void set_default_match_type(WaiverMatchType type) {
        default_match_type_ = type;
    }

private:
    std::vector<Waiver> waivers_;
    WaiverMatchType default_match_type_ = WaiverMatchType::Substring;

    static bool is_expired(const std::string& expiry);
    static bool fields_match(const std::string& a, const std::string& b);
    static bool fields_match_wildcard(const std::string& pattern, const std::string& value);
    static bool fields_match_regex(const std::string& pattern, const std::string& value);
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_WAIVER_H
