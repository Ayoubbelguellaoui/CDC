#include "cdc/reconvergence.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

#include "cdc/synchronizer.h"

namespace opencdc::cdc {

std::vector<uint64_t> ReconvergenceAnalyzer::find_fanout_sources(
    const ir::Graph& graph, const std::vector<clock::ClockDomain>& domains) const {
    std::vector<uint64_t> sources;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;

        auto succs = graph.register_successors(node.id);
        if (succs.size() < 2)
            continue;

        bool crosses_domain = false;
        for (uint64_t s : succs) {
            const ir::Node* sn = graph.find_node(s);
            if (!sn || sn->kind != ir::NodeKind::Register)
                continue;
            if (sn->clock_domain != node.clock_domain) {
                crosses_domain = true;
                break;
            }
        }
        if (crosses_domain)
            sources.push_back(node.id);
    }

    return sources;
}

std::vector<Finding> ReconvergenceAnalyzer::check_pairs(
    const ir::Graph& graph, const std::vector<clock::ClockDomain>& domains,
    const std::vector<Finding>& crossings, uint64_t source_id) const {
    std::vector<Finding> findings;
    const ir::Node* src = graph.find_node(source_id);
    if (!src)
        return findings;

    std::vector<const Finding*> src_crossings;
    for (const auto& f : crossings) {
        if (f.source_reg_id == source_id && f.rule_id == "CDC001")
            src_crossings.push_back(&f);
    }

    if (src_crossings.size() < 2)
        return findings;

    // Perf guard: high-fanout sources (1k dests -> 500k pairs) are catastrophic.
    // Cap pairs per source; excess pairs are skipped (documented truncation).
    static constexpr size_t kMaxPairsPerSource = 10000;
    size_t pair_count = 0;

    // Use the canonical SynchronizerMatcher rather than duplicating chain detection
    // inline. The previous lambda checked for independent successors rather than a
    // proper register chain (dest→s1→s2), which could suppress CDC003 incorrectly.
    SynchronizerMatcher sync_matcher;
    auto has_sync_chain = [&](uint64_t dest_id) -> bool {
        return sync_matcher.find_pattern_for_dest(dest_id, graph, /*strict=*/false) !=
               SyncPattern::None;
    };

    for (size_t i = 0; i < src_crossings.size(); ++i) {
        for (size_t j = i + 1; j < src_crossings.size(); ++j) {
            if (++pair_count > kMaxPairsPerSource)
                return findings;
            const Finding* a = src_crossings[i];
            const Finding* b = src_crossings[j];

            if (a->dest_reg_id == b->dest_reg_id)
                continue;
            // A handshake payload is held while it is sampled: its copies
            // cannot disagree, wherever they meet.
            if (a->handshake_payload || b->handshake_payload)
                continue;

            // Two synchronized single-bit paths of one source are taken as
            // safe unless report_double_sync_: independent synchronizers may
            // resolve the same transition in different cycles, so the copies
            // disagree for a cycle where they meet (the safe form,
            // synchronize once and fan out, is one crossing). A bus split
            // across synchronizers reconverges with bit skew either way.
            if (!report_double_sync_ && src->width <= 1 && has_sync_chain(a->dest_reg_id) &&
                has_sync_chain(b->dest_reg_id))
                continue;

            auto da_desc = meeting_points(graph, a->dest_reg_id);
            auto db_desc = meeting_points(graph, b->dest_reg_id);

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
                        f.reconvergence.explanation = "Multi-bit source '" + src->hier_name +
                                                      "' (width=" + std::to_string(src->width) +
                                                      ") fans out through independent synchronizer "
                                                      "paths that reconverge at '" +
                                                      consumer_name + "'.";
                    } else if (has_sync_chain(a->dest_reg_id) && has_sync_chain(b->dest_reg_id)) {
                        const ir::Node* da = graph.find_node(a->dest_reg_id);
                        const ir::Node* dbn = graph.find_node(b->dest_reg_id);
                        f.reconvergence.explanation =
                            "Single-bit source '" + src->hier_name + "' is synchronized twice ('" +
                            (da ? da->hier_name : "?") + "', '" + (dbn ? dbn->hier_name : "?") +
                            "'); the copies can resolve in different cycles and reconverge at '" +
                            consumer_name + "'. Synchronize once and fan out.";
                    } else {
                        f.reconvergence.explanation =
                            "Single-bit source '" + src->hier_name +
                            "' fans out through independent paths that reconverge at '" +
                            consumer_name + "'.";
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

std::vector<uint64_t> ReconvergenceAnalyzer::meeting_points(const ir::Graph& graph,
                                                            uint64_t start_id) const {
    std::vector<uint64_t> result;
    std::queue<std::pair<uint64_t, int>> bfs;
    std::unordered_set<uint64_t> visited{start_id};
    std::unordered_set<uint64_t> ports;
    bfs.push({start_id, 0});
    while (!bfs.empty() && result.size() < 32) {
        auto [cur, depth] = bfs.front();
        bfs.pop();
        if (depth > 0)
            result.push_back(cur);
        // Top-level outputs this register drives through nets and logic.
        std::vector<uint64_t> walk = graph.successors(cur);
        std::unordered_set<uint64_t> seen{cur};
        while (!walk.empty()) {
            uint64_t w = walk.back();
            walk.pop_back();
            if (!seen.insert(w).second)
                continue;
            const ir::Node* n = graph.find_node(w);
            if (!n || n->kind == ir::NodeKind::Register)
                continue;
            if (n->kind == ir::NodeKind::Port &&
                std::count(n->hier_name.begin(), n->hier_name.end(), '.') == 1) {
                if (ports.insert(w).second)
                    result.push_back(w);
                continue;
            }
            for (uint64_t x : graph.successors(w))
                walk.push_back(x);
        }
        if (depth >= max_hops_)
            continue;
        for (uint64_t s : graph.register_successors(cur)) {
            if (visited.insert(s).second)
                bfs.push({s, depth + 1});
        }
    }
    return result;
}

bool ReconvergenceAnalyzer::derives_from(const ir::Graph& graph, uint64_t to, uint64_t from) const {
    const ir::Node* t = graph.find_node(to);
    if (!t)
        return false;
    std::vector<std::pair<uint64_t, int>> stack{{to, 0}};
    std::unordered_set<uint64_t> visited{to};
    while (!stack.empty()) {
        auto [cur, depth] = stack.back();
        stack.pop_back();
        if (depth >= max_hops_)
            continue;
        for (uint64_t p : graph.register_predecessors(cur, true, true)) {
            if (p == from)
                return true;
            const ir::Node* pn = graph.find_node(p);
            if (pn && pn->clock_domain == t->clock_domain && visited.insert(p).second)
                stack.push_back({p, depth + 1});
        }
    }
    return false;
}

std::vector<Finding> ReconvergenceAnalyzer::check_correlated(
    const ir::Graph& graph, const std::vector<Finding>& crossings) const {
    std::vector<Finding> findings;
    std::vector<const Finding*> cdc;
    for (const auto& f : crossings) {
        if (f.rule_id == "CDC001")
            cdc.push_back(&f);
    }
    static constexpr size_t kMaxPairs = 100000;
    size_t pairs = 0;
    std::unordered_set<uint64_t> reported;  // packed source pair
    for (size_t i = 0; i < cdc.size(); ++i) {
        for (size_t j = i + 1; j < cdc.size(); ++j) {
            if (++pairs > kMaxPairs)
                return findings;
            const Finding* a = cdc[i];
            const Finding* b = cdc[j];
            if (a->handshake_payload || b->handshake_payload)
                continue;
            if (a->source_reg_id == b->source_reg_id || a->dest_reg_id == b->dest_reg_id ||
                a->source_domain != b->source_domain || a->dest_domain != b->dest_domain)
                continue;
            uint64_t lo = std::min(a->source_reg_id, b->source_reg_id);
            uint64_t hi = std::max(a->source_reg_id, b->source_reg_id);
            uint64_t key = (lo << 32) | hi;
            if (reported.count(key))
                continue;
            const Finding* early = a;
            const Finding* late = b;
            if (!derives_from(graph, late->source_reg_id, early->source_reg_id)) {
                std::swap(early, late);
                if (!derives_from(graph, late->source_reg_id, early->source_reg_id))
                    continue;
            }
            auto pa = meeting_points(graph, early->dest_reg_id);
            std::unordered_set<uint64_t> pa_set(pa.begin(), pa.end());
            for (uint64_t m : meeting_points(graph, late->dest_reg_id)) {
                if (!pa_set.count(m))
                    continue;
                const ir::Node* meet = graph.find_node(m);
                Finding f;
                f.rule_id = "CDC003";
                f.rule_name = "reconvergence_hazard";
                f.severity = "warning";
                f.source_reg_id = early->source_reg_id;
                f.source_reg_name = early->source_reg_name;
                f.source_domain = early->source_domain;
                f.dest_domain = early->dest_domain;
                f.source_loc = early->source_loc;
                f.reconvergence.is_reconvergent = true;
                f.reconvergence.common_source_id = early->source_reg_id;
                f.reconvergence.common_source_name = early->source_reg_name;
                f.reconvergence.explanation =
                    "Correlated sources '" + early->source_reg_name + "' and '" +
                    late->source_reg_name + "' ('" + late->source_reg_name + "' derives from '" +
                    early->source_reg_name + "' in '" + early->source_domain +
                    "') are synchronized separately ('" + early->dest_reg_name + "', '" +
                    late->dest_reg_name + "') and reconverge at '" +
                    (meet ? meet->hier_name : "<unknown>") +
                    "': their relation is lost across the crossing.";
                f.reason = f.reconvergence.explanation;
                findings.push_back(std::move(f));
                reported.insert(key);
                break;
            }
        }
    }
    return findings;
}

std::vector<Finding> ReconvergenceAnalyzer::analyze(const ir::Graph& graph,
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
    for (auto& f : check_correlated(graph, crossings))
        findings.push_back(std::move(f));

    return findings;
}

}  // namespace opencdc::cdc
