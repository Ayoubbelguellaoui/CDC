#ifndef OPENCDC_ANALYSIS_TREND_H
#define OPENCDC_ANALYSIS_TREND_H

#include "cdc/crossing.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <ctime>

namespace opencdc::analysis {

struct Baseline {
    std::string name;
    std::time_t timestamp = 0;
    std::string version;
    std::vector<cdc::Finding> findings;
    std::unordered_map<std::string, int> rule_counts;
    int total_errors = 0;
    int total_warnings = 0;
    int total_waived = 0;
};

struct TrendReport {
    int new_findings = 0;
    int fixed_findings = 0;
    int persistent_findings = 0;
    int total_baseline = 0;
    int total_current = 0;
    
    std::vector<cdc::Finding> added;
    std::vector<cdc::Finding> removed;
    std::vector<cdc::Finding> unchanged;
    
    std::unordered_map<std::string, int> rule_delta;
    
    bool improved() const { return new_findings < fixed_findings; }
    bool regressed() const { return new_findings > fixed_findings; }
    bool stable() const { return new_findings == fixed_findings; }
    
    std::string summary() const;
};

class TrendAnalyzer {
public:
    void save_baseline(const std::string& name,
                      const std::vector<cdc::Finding>& findings,
                      const std::string& filepath);
    
    Baseline load_baseline(const std::string& filepath);
    
    TrendReport compare(const Baseline& baseline,
                       const std::vector<cdc::Finding>& current);
    
    TrendReport compare(const std::string& baseline_file,
                       const std::vector<cdc::Finding>& current);
    
    std::vector<Baseline> list_baselines(const std::string& directory);
    
    bool delete_baseline(const std::string& filepath);
    
private:
    std::string serialize_finding(const cdc::Finding& f);
    cdc::Finding deserialize_finding(const std::string& s);
    
    std::string finding_key(const cdc::Finding& f);
    
    std::unordered_map<std::string, cdc::Finding> build_finding_map(
        const std::vector<cdc::Finding>& findings);
};

} // namespace opencdc::analysis

#endif // OPENCDC_ANALYSIS_TREND_H
