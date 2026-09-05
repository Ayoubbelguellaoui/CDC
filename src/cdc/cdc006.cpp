#include "cdc/cdc006.h"

namespace opencdc::cdc {

std::vector<Finding> Cdc006Analyzer::analyze(
    const ir::Graph& graph, const std::vector<clock::ClockDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain) {
    std::vector<Finding> findings;

    auto chains = sync_matcher_.match(graph);

    for (const auto& chain : chains) {
        if (chain.stage_ids.empty())
            continue;
        if (chain.pattern == SyncPattern::None)
            continue;

        uint64_t first_stage_id = chain.stage_ids[0];
        const ir::Node* first_stage = graph.find_node(first_stage_id);
        if (!first_stage)
            continue;

        // Check for combinational logic between ALL adjacent stage pairs
        for (size_t idx = 1; idx < chain.stage_ids.size(); ++idx) {
            uint64_t stage_id = chain.stage_ids[idx];

            const ir::Node* stage = graph.find_node(stage_id);
            if (!stage)
                continue;

            // If stage has any combinational predecessor, the chain is corrupted
            bool has_comb_pred = false;
            for (uint64_t p : graph.predecessors(stage_id)) {
                const ir::Node* pn = graph.find_node(p);
                if (!pn)
                    continue;
                if (pn->kind == ir::NodeKind::Combinational) {
                    has_comb_pred = true;
                    break;
                }
            }
            if (!has_comb_pred)
                continue;

            Finding f;
            f.rule_id = "CDC006";
            f.rule_name = "combinational_between_sync";
            f.severity = "error";
            f.source_reg_id = 0;
            f.dest_reg_id = stage_id;
            f.dest_reg_name = stage->hier_name;
            f.dest_domain = stage->clock_domain;
            f.path.node_ids = {stage_id};
            f.source_loc = stage->loc;

            std::string chain_type;
            switch (chain.pattern) {
                case SyncPattern::TwoFF:
                    chain_type = "2FF";
                    break;
                case SyncPattern::FourFF:
                    chain_type = "4FF";
                    break;
                default:
                    chain_type = "3FF";
                    break;
            }
            f.reason = "Synchronizer chain " + chain_type + " at '" +
                       first_stage->hier_name +
                       "' has combinational logic feeding stage " +
                       std::to_string(idx + 1) +
                       " ('" + stage->hier_name +
                       "'). "
                       "Combinational logic between synchronization stages defeats the "
                       "purpose of the synchronizer.";
            f.safety_status = SafetyStatus::VerifiedUnsafe;
            f.safety_provenance =
                "Combinational logic detected between synchronizer stages";
            findings.push_back(std::move(f));
        }

        // R6: Check for combinational logic BEFORE the first sync stage
        for (uint64_t pred_id : graph.predecessors(first_stage_id)) {
            const ir::Node* pred = graph.find_node(pred_id);
            if (!pred || pred->kind != ir::NodeKind::Combinational)
                continue;

            // Check if this combinational node is driven by a cross-domain register
            for (uint64_t comb_pred : graph.predecessors(pred_id)) {
                const ir::Node* cp = graph.find_node(comb_pred);
                if (cp && cp->kind == ir::NodeKind::Register &&
                    cp->clock_domain != first_stage->clock_domain) {
                    Finding f;
                    f.rule_id = "CDC006";
                    f.rule_name = "combinational_before_sync";
                    f.severity = "error";
                    f.source_reg_id = comb_pred;
                    f.dest_reg_id = first_stage_id;
                    f.source_reg_name = cp->hier_name;
                    f.dest_reg_name = first_stage->hier_name;
                    f.source_domain = cp->clock_domain;
                    f.dest_domain = first_stage->clock_domain;
                    f.path.node_ids = {comb_pred, pred_id, first_stage_id};
                    f.source_loc = cp->loc;
                    f.bus_width = cp->width;

                    std::string chain_type;
                    switch (chain.pattern) {
                        case SyncPattern::TwoFF:
                            chain_type = "2FF";
                            break;
                        case SyncPattern::FourFF:
                            chain_type = "4FF";
                            break;
                        default:
                            chain_type = "3FF";
                            break;
                    }
                    f.reason = "Combinational logic '" + pred->hier_name +
                               "' between cross-domain source '" + cp->hier_name +
                               "' and first synchronizer stage '" + first_stage->hier_name +
                               "' in " + chain_type + " chain. "
                               "Combinational logic before the first sync stage may "
                               "propagate metastability.";
                    f.safety_status = SafetyStatus::VerifiedUnsafe;
                    f.safety_provenance =
                        "Combinational logic before first synchronizer stage";

                    findings.push_back(std::move(f));
                    break;
                }
            }
        }
    }

    return findings;
}

}  // namespace opencdc::cdc
