#include "cdc/waiver.h"
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace opencdc::cdc {

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

static std::tm parse_date(const std::string& date) {
    std::tm t = {};
    if (date.size() == 10 && date[4] == '-' && date[7] == '-') {
        t.tm_year = std::stoi(date.substr(0, 4)) - 1900;
        t.tm_mon = std::stoi(date.substr(5, 2)) - 1;
        t.tm_mday = std::stoi(date.substr(8, 2));
    }
    return t;
}

bool WaiverEngine::is_expired(const std::string& expiry) {
    if (expiry.empty()) return false;

    std::tm exp = parse_date(expiry);
    if (exp.tm_year == -1900) return false;

    std::time_t exp_time = std::mktime(&exp);
    std::time_t now = std::time(nullptr);
    return std::difftime(now, exp_time) > 0;
}

bool WaiverEngine::fields_match(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return true;
    return to_lower(a) == to_lower(b);
}

bool WaiverEngine::matches(const Finding& f, const Waiver& w) const {
    if (!fields_match(w.rule_id, f.rule_id)) return false;
    if (!fields_match(w.source_reg_name, f.source_reg_name)) return false;
    if (!fields_match(w.dest_reg_name, f.dest_reg_name)) return false;
    if (!fields_match(w.source_domain, f.source_domain)) return false;
    if (!fields_match(w.dest_domain, f.dest_domain)) return false;
    if (is_expired(w.expiry)) return false;
    return true;
}

void WaiverEngine::add_waiver(const Waiver& w) {
    waivers_.push_back(w);
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

bool WaiverEngine::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        Waiver w;
        iss >> w.rule_id >> w.source_reg_name >> w.dest_reg_name
            >> w.source_domain >> w.dest_domain;

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

        waivers_.push_back(std::move(w));
    }

    return true;
}

} // namespace opencdc::cdc
