#ifndef OPENCDC_RULES_RULE_H
#define OPENCDC_RULES_RULE_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cdc/crossing.h"

namespace opencdc::rules {

using cdc::Finding;

struct Rule {
    std::string id;
    std::string name;
    std::string description;
    std::string severity;
    bool enabled = true;
    std::string finding_type;
    std::string version;
    bool severity_overridden = false;
};

struct RuleOverride {
    std::string rule_id;
    std::string severity;
    bool set_enabled = false;
    bool enabled = true;
};

class RuleEngine {
   public:
    RuleEngine();

    void add_override(const RuleOverride& override);
    std::vector<Finding> filter(const std::vector<Finding>& findings) const;

    std::optional<Rule> find_rule(const std::string& rule_id) const;
    const std::vector<Rule>& rules() const {
        return rules_;
    }

    bool is_enabled(const std::string& rule_id) const;

   private:
    std::vector<Rule> rules_;
    std::unordered_map<std::string, size_t> rule_index_;

    void register_rule(const Rule& r);
    void apply_override(const RuleOverride& o);
};

}  // namespace opencdc::rules

#endif  // OPENCDC_RULES_RULE_H
