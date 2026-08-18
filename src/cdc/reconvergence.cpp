#include "cdc/reconvergence.h"
#include <unordered_set>

namespace opencdc::cdc {

std::vector<uint64_t> ReconvergenceAnalyzer::find_fanout_sources(
    const ir::Graph& graph,
    const std::vector<clock::ClockDomain>& domains) const {
    std::vector<uint64_t> sources;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register) continue;

        auto succs = graph.successors(node.id);
        if (succs.size() < 2) continue;

        bool crosses_domain = false;
        for (uint64_t s : succs) {
            const ir::Node* sn = graph.find_node(s);
            if (!sn || sn->kind != ir::NodeKind::Register) continue;
            if (sn->clock_domain != node.clock_domain) {
                crosses_domain = true;
                break;
            }
        }
        if (crosses_domain) sources.push_back(node.id);
    }

    return sources;
}

std::vector<Finding> ReconvergenceAnalyzer::check_pairs(
    const ir::Graph& graph,
    const std::vector<clock::ClockDomain>& domains,
    const std::vector<Finding>& crossings,
    uint64_t source_id) const {
    std::vector<Finding> findings;
    const ir::Node* src = graph.find_node(source_id);
    if (!src) return findings;

    std::vector<const Finding*> src_crossings;
    for (const auto& f : crossings) {
        if (f.source_reg_id == source_id)
            src_crossings.push_back(&f);
    }

    if (src_crossings.size() < 2) return findings;

    auto has_sync_chain = [&](uint64_t dest_id) -> bool {
        const ir::Node* dest = graph.find_node(dest_id);
        if (!dest) return false;
        for (uint64_t s : graph.successors(dest_id)) {
            const ir::Node* sn = graph.find_node(s);
            if (!sn || sn->kind != ir::NodeKind::Register) continue;
            if (sn->clock_domain != dest->clock_domain) continue;
            for (uint64_t s2 : graph.successors(s)) {
                const ir::Node* sn2 = graph.find_node(s2);
                if (!sn2 || sn2->kind != ir::NodeKind::Register) continue;
                if (sn2->clock_domain != dest->clock_domain) continue;
                return true;
            }
        }
        return false;
    };

    for (size_t i = 0; i < src_crossings.size(); ++i) {
        for (size_t j = i + 1; j < src_crossings.size(); ++j) {
            const Finding* a = src_crossings[i];
            const Finding* b = src_crossings[j];

            if (a->dest_reg_id == b->dest_reg_id) continue;

            if (has_sync_chain(a->dest_reg_id) || has_sync_chain(b->dest_reg_id))
                continue;

            const ir::Node* da = graph.find_node(a->dest_reg_id);
            const ir::Node* db = graph.find_node(b->dest_reg_id);
            if (!da || !db) continue;

            auto da_succs = graph.successors(a->dest_reg_id);
            auto db_succs = graph.successors(b->dest_reg_id);

            for (uint64_t da_s : da_succs) {
                for (uint64_t db_s : db_succs) {
                    if (da_s == db_s) {
                        Finding f;
                        f.rule_id = "CDC003";
                        f.rule_name = "reconvergence_hazard";
                        f.severity = "warning";
                        f.source_reg_id = source_id;
                        f.source_reg_name = src->hier_name;
                        f.source_domain = a->source_domain;
                        f.reconvergence.is_reconvergent = true;
                        f.reconvergence.common_source_id = source_id;
                        f.reconvergence.common_source_name = src->hier_name;
                        f.reconvergence.is_hazardous = (src->width > 1);
                        f.source_loc = src->loc;

                        if (src->width > 1) {
                            f.reconvergence.explanation =
                                "Multi-bit source '" + src->hier_name +
                                "' (width=" + std::to_string(src->width) +
                                ") fans out through independent synchronizer paths that reconverge at '"
                                + graph.find_node(da_s)->hier_name + "'.";
                        } else {
                            f.reconvergence.explanation =
                                "Single-bit source '" + src->hier_name +
                                "' fans out through independent paths that reconverge at '"
                                + graph.find_node(da_s)->hier_name + "'.";
                        }

                        f.reason = f.reconvergence.explanation;
                        findings.push_back(std::move(f));
                    }
                }
            }
        }
    }

    return findings;
}

std::vector<Finding> ReconvergenceAnalyzer::analyze(
    const ir::Graph& graph,
    const std::vector<clock::ClockDomain>& domains,
    const std::vector<Finding>& crossings) {
    std::vector<Finding> findings;
    auto sources = find_fanout_sources(graph, domains);

    for (uint64_t src_id : sources) {
        auto new_findings = check_pairs(graph, domains, crossings, src_id);
        for (auto& f : new_findings) {
            findings.push_back(std::move(f));
        }
    }

    return findings;
}

} // namespace opencdc::cdc
