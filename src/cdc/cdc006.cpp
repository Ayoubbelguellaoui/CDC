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

        std::string chain_type = "sync";
        if (chain.pattern == SyncPattern::TwoFF)
            chain_type = "2FF";
        else if (chain.pattern == SyncPattern::ThreeFF)
            chain_type = "3FF";
        else if (chain.pattern == SyncPattern::FourFF)
            chain_type = "4FF";
        else if (chain.pattern == SyncPattern::NStage)
            chain_type = "N-stage";

        // Check every stage for combinational fan-in (not just first->second),
        // so comb between stage2 and stage3 also fires.
        for (size_t si = 0; si < chain.stage_ids.size(); ++si) {
            uint64_t stage_id = chain.stage_ids[si];
            const ir::Node* stage = graph.find_node(stage_id);
            if (!stage)
                continue;

            bool has_comb_pred = false;
            for (uint64_t p : graph.predecessors(stage_id)) {
                const ir::Node* pn = graph.find_node(p);
                if (pn && pn->kind == ir::NodeKind::Combinational) {
                    has_comb_pred = true;
                    break;
                }
            }
            if (!has_comb_pred)
                continue;

            // Find cross-domain source driving the chain entry.
            uint64_t entry_id = chain.stage_ids[0];
            bool reported = false;
            for (uint64_t pred_id : graph.register_predecessors(entry_id)) {
                const ir::Node* pred = graph.find_node(pred_id);
                if (!pred || pred->kind != ir::NodeKind::Register)
                    continue;
                if (pred->clock_domain == stage->clock_domain)
                    continue;
                Finding f;
                f.rule_id = "CDC006";
                f.rule_name = "combinational_between_sync";
                f.severity = "error";
                f.source_reg_id = pred_id;
                f.dest_reg_id = stage_id;
                f.source_reg_name = pred->hier_name;
                f.dest_reg_name = stage->hier_name;
                f.source_domain = pred->clock_domain;
                f.dest_domain = stage->clock_domain;
                f.path.node_ids = {pred_id, stage_id};
                f.source_loc = pred->loc;
                f.bus_width = pred->width;
                f.safety_status = SafetyStatus::VerifiedUnsafe;
                f.safety_provenance = "Combinational logic between synchronizer stages";
                f.reason = "Synchronizer chain " + chain_type + " at '" +
                           stage->hier_name + "' has combinational logic between stages "
                           "driven by cross-domain source '" +
                           pred->hier_name + "'. "
                           "Combinational logic between synchronization stages defeats the "
                           "purpose of the synchronizer.";
                findings.push_back(std::move(f));
                reported = true;
                break;
            }
            if (reported)
                break;
        }
    }

    return findings;
}

}  // namespace opencdc::cdc
