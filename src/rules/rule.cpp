#include "rules/rule.h"

namespace opencdc::rules {

RuleEngine::RuleEngine() {
    register_rule({"CDC001", "unsynchronized_crossing",
                   "Register drives register across clock domains without synchronization",
                   "error", true, "crossing", "1.0.0"});
    register_rule({"CDC002", "multi_bit_crossing",
                   "Multi-bit bus crosses clock domains without gray-code or handshake",
                   "error", true, "crossing", "1.0.0"});
    register_rule({"CDC003", "reconvergence_hazard",
                   "Multiple paths from same source reconverge in destination domain",
                   "warning", true, "reconvergence", "1.0.0"});
}

void RuleEngine::register_rule(const Rule& r) {
    rules_.push_back(r);
    rule_index_[r.id] = rules_.size() - 1;
}

void RuleEngine::apply_override(const RuleOverride& o) {
    auto it = rule_index_.find(o.rule_id);
    if (it == rule_index_.end()) return;
    Rule& r = rules_[it->second];
    if (!o.severity.empty()) r.severity = o.severity;
    if (o.set_enabled) r.enabled = o.enabled;
}

void RuleEngine::add_override(const RuleOverride& override) {
    apply_override(override);
}

std::optional<Rule> RuleEngine::find_rule(const std::string& rule_id) const {
    auto it = rule_index_.find(rule_id);
    if (it == rule_index_.end()) return std::nullopt;
    return rules_[it->second];
}

bool RuleEngine::is_enabled(const std::string& rule_id) const {
    auto it = rule_index_.find(rule_id);
    if (it == rule_index_.end()) return true;
    return rules_[it->second].enabled;
}

std::vector<Finding> RuleEngine::filter(const std::vector<Finding>& findings) const {
    std::vector<Finding> result;
    for (const auto& f : findings) {
        auto it = rule_index_.find(f.rule_id);
        if (it == rule_index_.end()) {
            result.push_back(f);
            continue;
        }
        const Rule& r = rules_[it->second];
        if (!r.enabled) continue;
        Finding out = f;
        out.severity = r.severity;
        out.rule_name = r.name;
        result.push_back(std::move(out));
    }
    return result;
}

} // namespace opencdc::rules
