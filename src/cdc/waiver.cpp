#include "cdc/waiver.h"
#include "util/string_util.h"
#include <fstream>
#include <sstream>
#include <ctime>
#include <cctype>
#include <algorithm>
#include <regex>

namespace opencdc::cdc {

// Pull shared utilities into this translation unit's scope.
using opencdc::util::to_lower;
using opencdc::util::wildcard_match;

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Portable UTC time helpers: timegm/gmtime are POSIX-only; MSVC uses
// _mkgmtime/gmtime_s. gmtime/gmtime_r shared static buffers avoided so
// WaiverEngine stays safe under concurrent use.
#if defined(_WIN32)
static std::time_t time_utc(std::tm* t) { return _mkgmtime(t); }
static std::tm* to_utc(const std::time_t* t, std::tm* out) {
    return gmtime_s(out, t) == 0 ? out : nullptr;
}
#else
static std::time_t time_utc(std::tm* t) { return timegm(t); }
static std::tm* to_utc(const std::time_t* t, std::tm* out) {
    return gmtime_r(t, out);
}
#endif

static bool parse_date(const std::string& date, std::tm& t) {
    t = {};
    if (date.size() == 10 && date[4] == '-' && date[7] == '-') {
        try {
            t.tm_year = std::stoi(date.substr(0, 4)) - 1900;
            t.tm_mon = std::stoi(date.substr(5, 2)) - 1;
            t.tm_mday = std::stoi(date.substr(8, 2));
        } catch (...) {
            return false;
        }
        if (t.tm_year < 0 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31) return false;
        std::tm check = t;
        time_t normalized = time_utc(&check);
        std::tm roundtrip{};
        std::tm* rt = to_utc(&normalized, &roundtrip);
        return rt && rt->tm_year == t.tm_year && rt->tm_mon == t.tm_mon && rt->tm_mday == t.tm_mday;
    }
    return false;
}

bool WaiverEngine::is_expired(const std::string& expiry) {
    if (expiry.empty()) return false;

    std::tm exp;
    if (!parse_date(expiry, exp)) return true;

    std::time_t exp_time = time_utc(&exp);
    std::time_t now = std::time(nullptr);
    std::tm now_utc{};
    std::tm* now_tm = to_utc(&now, &now_utc);
    if (!now_tm) return true;
    std::time_t now_time = time_utc(now_tm);
    // Expiry date is inclusive: waiver stays valid through the end (UTC) of
    // the listed day, so a waiver dated today does not expire at midnight.
    return std::difftime(now_time, exp_time + 86400) >= 0;
}

bool WaiverEngine::fields_match(const std::string& waiver_field,
                                const std::string& finding_field) {
    // Empty waiver field acts as a wildcard (match any finding value).
    // Empty finding value can never satisfy a specific waiver pattern —
    // otherwise a truncated waiver line or an incomplete finding silently
    // waives unrelated results.
    if (waiver_field.empty()) return true;
    if (finding_field.empty()) return false;
    return to_lower(waiver_field) == to_lower(finding_field);
}

static bool substring_match(const std::string& pattern, const std::string& value) {
    if (pattern.empty()) return true;
    if (value.empty()) return false;
    return to_lower(value).find(to_lower(pattern)) != std::string::npos;
}

// Rule ids in this tool have the shape CDC001 (letter prefix + digits); "*"
// is an explicit waive-all-rules wildcard. Anything else almost certainly
// comes from a field-shifted malformed waiver line and must be rejected
// instead of silently matching (or never matching) findings.
static bool is_valid_rule_id(const std::string& id) {
    if (id == "*") return true;
    if (id.size() < 3) return false;
    size_t i = 0;
    for (; i < id.size() && !std::isdigit(static_cast<unsigned char>(id[i])); ++i) {
        if (!std::isalpha(static_cast<unsigned char>(id[i]))) return false;
    }
    if (i < 2) return false;  // require at least two letters
    if (i == id.size()) return false;  // no digits found
    for (; i < id.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(id[i]))) return false;
    }
    return true;
}

bool WaiverEngine::fields_match_wildcard(const std::string& pattern, const std::string& value) {
    if (pattern.empty()) return true;
    if (value.empty()) return false;
    if (pattern == "*") return true;
    // Delegate to the shared case-insensitive implementation in util/string_util.h.
    return wildcard_match(pattern, value);
}

// fields_match_regex: legacy path — only reached if a Regex waiver somehow
// has no pre-compiled regex (e.g. waivers added directly without add_waiver).
// The normal path in matches() uses Waiver::source_regex / dest_regex instead.
bool WaiverEngine::fields_match_regex(const std::string& pattern, const std::string& value) {
    if (pattern.empty()) return true;
    if (value.empty()) return false;
    try {
        std::regex re(pattern, std::regex::icase);
        return std::regex_match(value, re);
    } catch (const std::regex_error&) {
        return to_lower(pattern) == to_lower(value);
    }
}


bool WaiverEngine::matches(const Finding& f, const Waiver& w) const {
    // A waiver with no rule id would match every finding of every rule.
    // Waive-all-rules must be expressed explicitly with "*".
    if (w.rule_id.empty()) return false;

    if (!fields_match(w.rule_id, f.rule_id)) return false;

    if (w.match_type == WaiverMatchType::Substring) {
        if (!substring_match(w.source_reg_name, f.source_reg_name)) return false;
        if (!substring_match(w.dest_reg_name, f.dest_reg_name)) return false;
    } else if (w.match_type == WaiverMatchType::Wildcard) {
        if (!w.source_reg_name.empty()) {
            if (!fields_match_wildcard(w.source_reg_name, f.source_reg_name)) return false;
        }
        if (!w.dest_reg_name.empty()) {
            if (!fields_match_wildcard(w.dest_reg_name, f.dest_reg_name)) return false;
        }
    } else if (w.match_type == WaiverMatchType::Regex) {
        // Use pre-compiled regex stored at add_waiver time (never recompile per call).
        if (w.source_regex) {
            if (!std::regex_match(f.source_reg_name, *w.source_regex)) return false;
        } else if (!w.source_reg_name.empty()) {
            if (to_lower(w.source_reg_name) != to_lower(f.source_reg_name)) return false;
        }
        if (w.dest_regex) {
            if (!std::regex_match(f.dest_reg_name, *w.dest_regex)) return false;
        } else if (!w.dest_reg_name.empty()) {
            if (to_lower(w.dest_reg_name) != to_lower(f.dest_reg_name)) return false;
        }
    }

    if (!fields_match(w.source_domain, f.source_domain)) return false;
    if (!fields_match(w.dest_domain, f.dest_domain)) return false;
    if (is_expired(w.expiry)) return false;
    return true;
}

bool WaiverEngine::add_waiver(const Waiver& w) {
    // Reject waivers that could silently over-match (no/invalid rule id).
    if (!is_valid_rule_id(w.rule_id)) return false;

    Waiver stored = w;
    if (w.match_type == WaiverMatchType::Regex) {
        // Compile regex once at add time — not per match call.
        try {
            if (!w.source_reg_name.empty())
                stored.source_regex = std::make_shared<std::regex>(
                    w.source_reg_name, std::regex::icase);
            if (!w.dest_reg_name.empty())
                stored.dest_regex = std::make_shared<std::regex>(
                    w.dest_reg_name, std::regex::icase);
        } catch (const std::regex_error&) {
            return false;
        }
    }

    waivers_.push_back(std::move(stored));
    return true;
}

std::vector<Finding> WaiverEngine::apply(const std::vector<Finding>& findings) const {
    std::vector<Finding> result;
    for (auto f : findings) {
        for (const auto& w : waivers_) {
            if (matches(f, w)) {
                f.waived = true;
                f.waiver_justification = w.justification;
                f.waiver_owner = w.owner;
                break;
            }
        }
        result.push_back(std::move(f));
    }
    return result;
}

std::vector<std::string> WaiverEngine::check_unused(
    const std::vector<Finding>& findings) const {
    std::vector<std::string> warnings;
    for (size_t i = 0; i < waivers_.size(); ++i) {
        const auto& w = waivers_[i];
        bool used = false;
        for (const auto& f : findings) {
            if (matches(f, w)) { used = true; break; }
        }
        if (is_expired(w.expiry)) {
            // Always flag expired waivers — they no longer protect anything
            // and must be removed or renewed for compliance.
            warnings.push_back("Waiver #" + std::to_string(i + 1) +
                               " (rule=" + w.rule_id +
                               ", source=" + w.source_reg_name +
                               ", dest=" + w.dest_reg_name +
                               ", expiry=" + w.expiry +
                               ") has expired and is no longer active");
        } else if (!used) {
            warnings.push_back("Waiver #" + std::to_string(i + 1) +
                               " (rule=" + w.rule_id +
                               ", source=" + w.source_reg_name +
                               ", dest=" + w.dest_reg_name +
                               ") did not match any finding");
        }
    }
    return warnings;
}

bool WaiverEngine::load_from_file(const std::string& path, std::string* error) {
    auto fail = [&error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    std::ifstream file(path);
    if (!file.is_open()) return fail("cannot open waiver file: " + path);

    constexpr size_t kMaxLineLength = 65536;
    size_t loaded = 0;
    size_t line_no = 0;
    std::string first_error;

    std::string line;
    while (std::getline(file, line)) {
        ++line_no;
        if (line.size() > kMaxLineLength) {
            if (first_error.empty())
                first_error = "waiver line " + std::to_string(line_no) +
                              " exceeds 64KB limit";
            continue;
        }
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        Waiver w;
        w.match_type = default_match_type_;

        std::string first_token;
        iss >> first_token;

        if (first_token == "REGEX" || first_token == "regex") {
            w.match_type = WaiverMatchType::Regex;
            iss >> w.rule_id >> w.source_reg_name >> w.dest_reg_name
                >> w.source_domain >> w.dest_domain;
        } else if (first_token == "WILDCARD" || first_token == "wildcard") {
            w.match_type = WaiverMatchType::Wildcard;
            iss >> w.rule_id >> w.source_reg_name >> w.dest_reg_name
                >> w.source_domain >> w.dest_domain;
        } else {
            w.rule_id = first_token;
            iss >> w.source_reg_name >> w.dest_reg_name
                >> w.source_domain >> w.dest_domain;
        }

        // A waiver with a missing/shifted rule id could match findings of the
        // wrong rule (or none) — reject it with a diagnostic instead.
        if (!is_valid_rule_id(w.rule_id)) {
            if (first_error.empty())
                first_error = "waiver line " + std::to_string(line_no) +
                              ": missing or invalid rule id";
            continue;
        }

        std::string rest;
        std::getline(iss, rest);
        rest = trim(rest);

        if (!rest.empty() && rest[0] == '"') {
            size_t end_quote = rest.find('"', 1);
            if (end_quote != std::string::npos) {
                w.justification = rest.substr(1, end_quote - 1);
                rest = trim(rest.substr(end_quote + 1));
            }
        }

        if (!rest.empty() && rest[0] == '@') {
            size_t space = rest.find(' ');
            w.owner = rest.substr(1, space != std::string::npos ? space - 1 : std::string::npos);
            if (space != std::string::npos) {
                rest = trim(rest.substr(space + 1));
            } else {
                rest.clear();
            }
        }

        if (!rest.empty()) {
            w.expiry = rest;
        }

        if (w.match_type == WaiverMatchType::Wildcard ||
            w.match_type == WaiverMatchType::Regex) {
            // Validate regex patterns at load time so we fail early on bad patterns.
            if (w.match_type == WaiverMatchType::Regex) {
                try {
                    if (!w.source_reg_name.empty())
                        std::regex(w.source_reg_name, std::regex::icase);
                    if (!w.dest_reg_name.empty())
                        std::regex(w.dest_reg_name, std::regex::icase);
                } catch (const std::regex_error&) {
                    if (first_error.empty())
                        first_error = "waiver line " + std::to_string(line_no) +
                                      ": invalid regular expression";
                    continue;
                }
            }
        }

        waivers_.push_back(std::move(w));
        ++loaded;
    }

    if (loaded == 0) {
        std::string msg = first_error.empty()
                              ? "no valid waivers loaded"
                              : first_error;
        return fail(msg + " (file: " + path + ")");
    }
    if (error) error->clear();
    return true;
}

} // namespace opencdc::cdc
