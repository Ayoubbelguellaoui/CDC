#include "cdc/reconvergence.h"
#include "cdc/synchronizer.h"
#include <unordered_set>
#include <queue>

namespace opencdc::cdc {

std::vector<uint64_t> ReconvergenceAnalyzer::find_fanout_sources(
    const ir::Graph& graph,
    const std::vector<clock::ClockDomain>& domains) const {
    std::vector<uint64_t> sources;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register) continue;

        auto succs = graph.register_successors(node.id);
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
        if (f.source_reg_id == source_id && f.rule_id == "CDC001")
            src_crossings.push_back(&f);
    }

    if (src_crossings.size() < 2) return findings;

    // Use the canonical SynchronizerMatcher rather than duplicating chain detection
    // inline. The previous lambda checked for independent successors rather than a
    // proper register chain (dest→s1→s2), which could suppress CDC003 incorrectly.
    SynchronizerMatcher sync_matcher;
    auto has_sync_chain = [&](uint64_t dest_id) -> bool {
        return sync_matcher.find_pattern_for_dest(dest_id, graph, /*strict=*/false)
               != SyncPattern::None;
    };

    for (size_t i = 0; i < src_crossings.size(); ++i) {
        for (size_t j = i + 1; j < src_crossings.size(); ++j) {
            const Finding* a = src_crossings[i];
            const Finding* b = src_crossings[j];

            if (a->dest_reg_id == b->dest_reg_id) continue;

            // A synchronizer chain on the destination only makes the crossing
            // safe for single-bit sources. A multi-bit bus split across
            // independent synchronizers still reconverges with bit skew, so
            // the hazard must be reported regardless of sync chains.
            if (src->width <= 1 &&
                (has_sync_chain(a->dest_reg_id) || has_sync_chain(b->dest_reg_id)))
                continue;

            // Bounded BFS: collect up to 16 register descendants per path (max 3 hops)
            auto collect_descendants = [&](uint64_t start_id) -> std::vector<uint64_t> {
                std::vector<uint64_t> result;
                std::queue<std::pair<uint64_t, int>> bfs;
                std::unordered_set<uint64_t> visited;
                bfs.push({start_id, 0});
                visited.insert(start_id);
                while (!bfs.empty() && result.size() < 16) {
                    auto [cur, depth] = bfs.front();
                    bfs.pop();
                    if (depth > 0 && graph.find_node(cur) &&
                        graph.find_node(cur)->kind == ir::NodeKind::Register) {
                        result.push_back(cur);
                    }
                    if (depth >= 3) continue;
                    for (uint64_t s : graph.register_successors(cur)) {
                        if (visited.insert(s).second) {
                            bfs.push({s, depth + 1});
                        }
                    }
                }
                return result;
            };

            auto da_desc = collect_descendants(a->dest_reg_id);
            auto db_desc = collect_descendants(b->dest_reg_id);

            std::unordered_set<uint64_t> da_set(da_desc.begin(), da_desc.end());
            for (uint64_t db_d : db_desc) {
                if (da_set.count(db_d)) {
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

                    const ir::Node* consumer = graph.find_node(db_d);
                    std::string consumer_name = consumer ? consumer->hier_name : "<unknown>";

                    if (src->width > 1) {
                        f.reconvergence.explanation =
                            "Multi-bit source '" + src->hier_name +
                            "' (width=" + std::to_string(src->width) +
                            ") fans out through independent synchronizer paths that reconverge at '"
                            + consumer_name + "'.";
                    } else {
                        f.reconvergence.explanation =
                            "Single-bit source '" + src->hier_name +
                            "' fans out through independent paths that reconverge at '"
                            + consumer_name + "'.";
                    }

                    f.reason = f.reconvergence.explanation;
                    findings.push_back(std::move(f));
                    break;  // one reconvergence per pair is enough
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
