#include "cdc/crossing.h"
#include <unordered_set>

namespace opencdc::cdc {

const clock::ClockDomain* CrossingAnalyzer::find_domain_for_node(
    uint64_t node_id,
    const std::vector<clock::ClockDomain>& domains) const {
    for (const auto& dom : domains) {
        for (uint64_t rid : dom.register_ids) {
            if (rid == node_id) return &dom;
        }
    }
    return nullptr;
}

std::string CrossingAnalyzer::build_reason(const Finding& f) const {
    std::string r = "Register '" + f.source_reg_name + "' in domain '" +
                    f.source_domain + "' drives register '" + f.dest_reg_name +
                    "' in domain '" + f.dest_domain +
                    "' without synchronization.";

    if (f.detected_sync == SyncPattern::None) {
        r += " No 2FF/3FF synchronizer chain detected on destination side.";
    }
    return r;
}

std::vector<Finding> CrossingAnalyzer::analyze(
    const ir::Graph& graph,
    const std::vector<clock::ClockDomain>& domains) {
    std::vector<Finding> findings;
    std::unordered_set<std::string> seen;

    for (const auto& src : graph.nodes()) {
        if (src.kind != ir::NodeKind::Register) continue;

        const clock::ClockDomain* src_dom =
            find_domain_for_node(src.id, domains);
        if (!src_dom) continue;

        for (uint64_t dst_id : graph.successors(src.id)) {
            const ir::Node* dst = graph.find_node(dst_id);
            if (!dst || dst->kind != ir::NodeKind::Register) continue;

            const clock::ClockDomain* dst_dom =
                find_domain_for_node(dst_id, domains);
            if (!dst_dom) continue;

            if (src_dom->id == dst_dom->id) continue;

            std::string key = std::to_string(src.id) + "->" + std::to_string(dst_id);
            if (seen.count(key)) continue;
            seen.insert(key);

            Finding f;
            f.rule_id = "CDC001";
            f.rule_name = "unsynchronized_crossing";
            f.severity = "error";
            f.source_reg_id = src.id;
            f.dest_reg_id = dst_id;
            f.source_reg_name = src.hier_name;
            f.dest_reg_name = dst->hier_name;
            f.source_domain = src_dom->name;
            f.dest_domain = dst_dom->name;
            f.path.node_ids = {src.id, dst_id};
            f.source_loc = src.loc;
            f.detected_sync = sync_matcher_.find_pattern_for_dest(dst_id, graph);

            f.reason = build_reason(f);
            findings.push_back(std::move(f));
        }
    }

    return findings;
}

} // namespace opencdc::cdc
