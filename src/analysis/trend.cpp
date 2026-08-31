#include "analysis/trend.h"
#include "opencdc/version.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <cstdint>

namespace opencdc::analysis {

std::string TrendAnalyzer::finding_key(const cdc::Finding& f) {
    std::ostringstream key;
    for (const auto& value : {f.rule_id, f.source_reg_name, f.dest_reg_name,
                              f.source_domain, f.dest_domain}) key << value.size() << ':' << value;
    return key.str();
}

static std::string hex_encode(const std::string& value) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}

static std::string hex_decode(const std::string& value) {
    if (value.size() % 2) return {};
    std::string result;
    for (size_t i = 0; i < value.size(); i += 2) {
        auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        int hi = digit(value[i]), lo = digit(value[i + 1]);
        if (hi < 0 || lo < 0) return {};
        result += static_cast<char>((hi << 4) | lo);
    }
    return result;
}

std::string TrendAnalyzer::serialize_finding(const cdc::Finding& f) {
    std::ostringstream oss;
    oss << "RULE64:" << hex_encode(f.rule_id) << "\n";
    oss << "NAME64:" << hex_encode(f.rule_name) << "\n";
    oss << "SEVERITY64:" << hex_encode(f.severity) << "\n";
    oss << "SOURCE64:" << hex_encode(f.source_reg_name) << "\n";
    oss << "DEST64:" << hex_encode(f.dest_reg_name) << "\n";
    oss << "SRC_DOMAIN64:" << hex_encode(f.source_domain) << "\n";
    oss << "DST_DOMAIN64:" << hex_encode(f.dest_domain) << "\n";
    oss << "REASON64:" << hex_encode(f.reason) << "\n";
    oss << "FILE64:" << hex_encode(f.source_loc.file) << "\n";
    oss << "LINE:" << f.source_loc.line << "\n";
    oss << "WIDTH:" << f.bus_width << "\n";
    oss << "WAIVED:" << (f.waived ? "1" : "0") << "\n";
    if (f.waived) {
        oss << "WAIVER_JUST64:" << hex_encode(f.waiver_justification) << "\n";
        oss << "WAIVER_OWNER64:" << hex_encode(f.waiver_owner) << "\n";
    }
    oss << "---\n";
    return oss.str();
}

cdc::Finding TrendAnalyzer::deserialize_finding(const std::string& s) {
    cdc::Finding f;
    std::istringstream iss(s);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        
        if (key == "RULE64") f.rule_id = hex_decode(value);
        else if (key == "NAME64") f.rule_name = hex_decode(value);
        else if (key == "SEVERITY64") f.severity = hex_decode(value);
        else if (key == "SOURCE64") f.source_reg_name = hex_decode(value);
        else if (key == "DEST64") f.dest_reg_name = hex_decode(value);
        else if (key == "SRC_DOMAIN64") f.source_domain = hex_decode(value);
        else if (key == "DST_DOMAIN64") f.dest_domain = hex_decode(value);
        else if (key == "REASON64") f.reason = hex_decode(value);
        else if (key == "FILE64") f.source_loc.file = hex_decode(value);
        else if (key == "WAIVER_JUST64") f.waiver_justification = hex_decode(value);
        else if (key == "WAIVER_OWNER64") f.waiver_owner = hex_decode(value);
        else if (key == "RULE") f.rule_id = value;
        else if (key == "NAME") f.rule_name = value;
        else if (key == "SEVERITY") f.severity = value;
        else if (key == "SOURCE") f.source_reg_name = value;
        else if (key == "DEST") f.dest_reg_name = value;
        else if (key == "SRC_DOMAIN") f.source_domain = value;
        else if (key == "DST_DOMAIN") f.dest_domain = value;
        else if (key == "REASON") f.reason = value;
        else if (key == "FILE") f.source_loc.file = value;
        else if (key == "LINE") { try { f.source_loc.line = std::stoul(value); } catch (...) {} }
        else if (key == "WIDTH") { try { f.bus_width = std::stoul(value); } catch (...) {} }
        else if (key == "WAIVED") f.waived = (value == "1");
        else if (key == "WAIVER_JUST") f.waiver_justification = value;
        else if (key == "WAIVER_OWNER") f.waiver_owner = value;
    }
    
    return f;
}

std::unordered_map<std::string, cdc::Finding> TrendAnalyzer::build_finding_map(
    const std::vector<cdc::Finding>& findings) {
    
    std::unordered_map<std::string, cdc::Finding> map;
    for (const auto& f : findings) {
        std::string key = finding_key(f);
        size_t occurrence = 0;
        while (map.find(key + '#' + std::to_string(occurrence)) != map.end()) ++occurrence;
        map[key + '#' + std::to_string(occurrence)] = f;
    }
    return map;
}

void TrendAnalyzer::save_baseline(const std::string& name,
                                  const std::vector<cdc::Finding>& findings,
                                  const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return;
    
    file << "BASELINE64:" << hex_encode(name) << "\n";
    file << "TIMESTAMP:" << std::time(nullptr) << "\n";
    file << "VERSION64:" << hex_encode(OPENCDC_VERSION) << "\n";
    file << "TOTAL:" << findings.size() << "\n";
    
    int errors = 0, warnings = 0, waived = 0;
    std::unordered_map<std::string, int> rule_counts;
    
    for (const auto& f : findings) {
        if (f.severity == "error") errors++;
        else if (f.severity == "warning") warnings++;
        if (f.waived) waived++;
        rule_counts[f.rule_id]++;
    }
    
    file << "ERRORS:" << errors << "\n";
    file << "WARNINGS:" << warnings << "\n";
    file << "WAIVED:" << waived << "\n";
    
    file << "RULES64:";
    bool first = true;
    for (const auto& [rule, count] : rule_counts) {
        if (!first) file << ",";
        file << hex_encode(rule) << "=" << count;
        first = false;
    }
    file << "\n";
    
    file << "FINDINGS_START\n";
    for (const auto& f : findings) {
        file << serialize_finding(f);
    }
    file << "FINDINGS_END\n";
}

Baseline TrendAnalyzer::load_baseline(const std::string& filepath) {
    Baseline baseline;
    std::ifstream file(filepath);
    if (!file.is_open()) return baseline;
    
    std::string line;
    std::unordered_map<std::string, int> rule_counts;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line == "FINDINGS_START") {
            std::ostringstream finding_stream;
            while (std::getline(file, line) && line != "FINDINGS_END") {
                if (line == "---") {
                    baseline.findings.push_back(deserialize_finding(finding_stream.str()));
                    finding_stream.str("");
                    finding_stream.clear();
                } else finding_stream << line << "\n";
            }
            continue;
        }
        
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        
        if (key == "BASELINE64") baseline.name = hex_decode(value);
        else if (key == "VERSION64") baseline.version = hex_decode(value);
        else if (key == "BASELINE") baseline.name = value;
        else if (key == "TIMESTAMP") { try { baseline.timestamp = std::stol(value); } catch (...) {} }
        else if (key == "VERSION") baseline.version = value;
        else if (key == "ERRORS") { try { baseline.total_errors = std::stoi(value); } catch (...) {} }
        else if (key == "WARNINGS") { try { baseline.total_warnings = std::stoi(value); } catch (...) {} }
        else if (key == "WAIVED") { try { baseline.total_waived = std::stoi(value); } catch (...) {} }
        else if (key == "RULES64" || key == "RULES") {
            std::istringstream rss(value);
            std::string rule_count;
            while (std::getline(rss, rule_count, ',')) {
                size_t eq = rule_count.find('=');
                if (eq != std::string::npos) {
                     std::string rule = rule_count.substr(0, eq);
                     if (key == "RULES64") rule = hex_decode(rule);
                    try {
                        int count = std::stoi(rule_count.substr(eq + 1));
                        baseline.rule_counts[rule] = count;
                    } catch (...) {}
                }
            }
        }
    }
    
    return baseline;
}

TrendReport TrendAnalyzer::compare(const Baseline& baseline,
                                   const std::vector<cdc::Finding>& current) {
    TrendReport report;
    report.total_baseline = baseline.findings.size();
    report.total_current = current.size();
    
    auto baseline_map = build_finding_map(baseline.findings);
    auto current_map = build_finding_map(current);
    
    for (const auto& [key, f] : current_map) {
        if (baseline_map.find(key) == baseline_map.end()) {
            report.new_findings++;
            report.added.push_back(f);
            report.rule_delta[f.rule_id]++;
        } else {
            report.persistent_findings++;
            report.unchanged.push_back(f);
        }
    }
    
    for (const auto& [key, f] : baseline_map) {
        if (current_map.find(key) == current_map.end()) {
            report.fixed_findings++;
            report.removed.push_back(f);
            report.rule_delta[f.rule_id]--;
        }
    }
    
    return report;
}

TrendReport TrendAnalyzer::compare(const std::string& baseline_file,
                                   const std::vector<cdc::Finding>& current) {
    Baseline baseline = load_baseline(baseline_file);
    return compare(baseline, current);
}

std::vector<Baseline> TrendAnalyzer::list_baselines(const std::string& directory) {
    std::vector<Baseline> baselines;
    
    DIR* dir = opendir(directory.c_str());
    if (!dir) return baselines;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.find(".baseline") != std::string::npos &&
            filename.size() > 8) {
            Baseline b = load_baseline(directory + "/" + filename);
            if (!b.name.empty()) {
                baselines.push_back(std::move(b));
            }
        }
    }
    
    closedir(dir);
    
    std::sort(baselines.begin(), baselines.end(),
              [](const Baseline& a, const Baseline& b) {
                  return a.timestamp > b.timestamp;
              });
    
    return baselines;
}

bool TrendAnalyzer::delete_baseline(const std::string& filepath) {
    return std::remove(filepath.c_str()) == 0;
}

std::string TrendReport::summary() const {
    std::ostringstream oss;
    oss << "Baseline: " << total_baseline << " findings\n";
    oss << "Current: " << total_current << " findings\n";
    oss << "New: " << new_findings << ", Fixed: " << fixed_findings << "\n";
    
    if (improved()) {
        oss << "Status: IMPROVED (";
    } else if (regressed()) {
        oss << "Status: REGRESSED (";
    } else {
        oss << "Status: STABLE (";
    }
    
    int delta = fixed_findings - new_findings;
    if (delta > 0) {
        oss << "+" << delta << " net fixes)\n";
    } else if (delta < 0) {
        oss << delta << " net regressions)\n";
    } else {
        oss << "no change)\n";
    }
    
    if (!rule_delta.empty()) {
        oss << "Rule changes:\n";
        for (const auto& [rule, delta] : rule_delta) {
            if (delta != 0) {
                oss << "  " << rule << ": ";
                if (delta > 0) oss << "+";
                oss << delta << "\n";
            }
        }
    }
    
    return oss.str();
}

} // namespace opencdc::analysis
