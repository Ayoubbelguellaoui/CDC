#include "cdc/synchronizer.h"

namespace opencdc::cdc {

bool SynchronizerMatcher::validate_2ff(const ir::Graph& graph, uint64_t s1, uint64_t s2) const {
    const ir::Node* n1 = graph.find_node(s1);
    const ir::Node* n2 = graph.find_node(s2);
    if (!n1 || !n2)
        return false;
    if (n1->kind != ir::NodeKind::Register || n2->kind != ir::NodeKind::Register)
        return false;
    if (n1->clock_domain != n2->clock_domain)
        return false;
    if (n1->clock_domain.empty())
        return false;

    auto succs = graph.register_successors(s1, false);
    for (uint64_t s : succs) {
        if (s == s2)
            return true;
    }
    return false;
}

bool SynchronizerMatcher::validate_3ff(const ir::Graph& graph, uint64_t s1, uint64_t s2,
                                       uint64_t s3) const {
    return validate_2ff(graph, s1, s2) && validate_2ff(graph, s2, s3);
}

SyncPattern SynchronizerMatcher::find_pattern_for_dest(uint64_t dest_reg_id, const ir::Graph& graph,
                                                       bool strict) const {
    const ir::Node* dest = graph.find_node(dest_reg_id);
    if (!dest)
        return SyncPattern::None;
    if (dest->clock_domain.empty())
        return SyncPattern::None;

    bool has_cross_domain_register_pred = false;
    for (uint64_t pred_id : graph.register_predecessors(dest_reg_id, false)) {
        const ir::Node* pred = graph.find_node(pred_id);
        if (pred && pred->kind == ir::NodeKind::Register &&
            pred->clock_domain != dest->clock_domain) {
            has_cross_domain_register_pred = true;
            break;
        }
    }
    if (!has_cross_domain_register_pred)
        return SyncPattern::None;

    // In strict mode, reject dest if it has same-domain register predecessors
    // (other than self) — this means data from the same domain leaks into the
    // synchronizer entry point, making it ineffective.
    if (strict) {
        for (uint64_t pred_id : graph.register_predecessors(dest_reg_id, false)) {
            if (pred_id == dest_reg_id)
                continue;
            const ir::Node* pred = graph.find_node(pred_id);
            if (pred && pred->kind == ir::NodeKind::Register &&
                pred->clock_domain == dest->clock_domain) {
                return SyncPattern::None;
            }
        }
    }

    uint64_t s1_id = 0;
    for (uint64_t s1 : graph.register_successors(dest_reg_id, false)) {
        const ir::Node* n1 = graph.find_node(s1);
        if (!n1 || n1->kind != ir::NodeKind::Register)
            continue;
        if (n1->clock_domain != dest->clock_domain)
            continue;

        if (strict) {
            bool has_unexpected_pred = false;
            for (uint64_t pred : graph.register_predecessors(s1, false)) {
                const ir::Node* pn = graph.find_node(pred);
                if (!pn || pn->kind != ir::NodeKind::Register)
                    continue;
                if (pred == dest_reg_id)
                    continue;
                if (pred == s1)
                    continue;
                if (pn->clock_domain == n1->clock_domain) {
                    has_unexpected_pred = true;
                    break;
                }
            }
            if (has_unexpected_pred)
                continue;
        }

        s1_id = s1;
        break;
    }
    if (!s1_id)
        return SyncPattern::None;

    uint64_t s2_id = 0;
    for (uint64_t s2 : graph.register_successors(s1_id, false)) {
        const ir::Node* n2 = graph.find_node(s2);
        if (!n2 || n2->kind != ir::NodeKind::Register)
            continue;
        if (n2->clock_domain != dest->clock_domain)
            continue;
        s2_id = s2;
        break;
    }
    if (!s2_id)
        return SyncPattern::TwoFF;

    uint64_t s3_id = 0;
    for (uint64_t s3 : graph.register_successors(s2_id, false)) {
        const ir::Node* n3 = graph.find_node(s3);
        if (!n3 || n3->kind != ir::NodeKind::Register)
            continue;
        if (n3->clock_domain != dest->clock_domain)
            continue;
        s3_id = s3;
        break;
    }
    if (!s3_id)
        return SyncPattern::ThreeFF;  // s1 + s2 exist — 3-stage chain (dest + s1 + s2)

    return SyncPattern::ThreeFF;  // s1, s2, and s3 all confirmed — 3+ FF chain
}

std::vector<SynchronizerChain> SynchronizerMatcher::match(const ir::Graph& graph) {
    std::vector<SynchronizerChain> chains;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;

        for (uint64_t pred_id : graph.register_predecessors(node.id, false)) {
            const ir::Node* pred = graph.find_node(pred_id);
            if (!pred || pred->kind != ir::NodeKind::Register)
                continue;
            if (pred->clock_domain == node.clock_domain)
                continue;

            SyncPattern pat = find_pattern_for_dest(node.id, graph, true);
            if (pat == SyncPattern::None)
                continue;

            bool exists = false;
            for (const auto& c : chains) {
                if (c.source_reg_id == pred_id && c.clock_domain == node.clock_domain) {
                    exists = true;
                    break;
                }
            }
            if (exists)
                continue;

            SynchronizerChain chain;
            chain.pattern = pat;
            chain.source_reg_id = pred_id;
            chain.clock_domain = node.clock_domain;
            chain.stage_ids.push_back(node.id);

            auto s1_succs = graph.register_successors(node.id, false);
            for (uint64_t s : s1_succs) {
                const ir::Node* n = graph.find_node(s);
                if (n && n->kind == ir::NodeKind::Register &&
                    n->clock_domain == node.clock_domain) {
                    chain.stage_ids.push_back(s);
                    if (pat == SyncPattern::ThreeFF) {
                        auto s2_succs = graph.register_successors(s, false);
                        for (uint64_t s2 : s2_succs) {
                            const ir::Node* n2 = graph.find_node(s2);
                            if (n2 && n2->kind == ir::NodeKind::Register &&
                                n2->clock_domain == node.clock_domain) {
                                chain.stage_ids.push_back(s2);
                                break;
                            }
                        }
                    }
                    break;
                }
            }

            chains.push_back(std::move(chain));
        }
    }

    return chains;
}

}  // namespace opencdc::cdc
