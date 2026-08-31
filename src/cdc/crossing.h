#ifndef OPENCDC_CDC_CROSSING_H
#define OPENCDC_CDC_CROSSING_H

#include "ir/graph.h"
#include "clock/domain.h"
#include "clock/constraints.h"
#include "cdc/synchronizer.h"
#include "cdc/pattern.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstddef>

namespace opencdc::cdc {

struct CrossingPath {
    std::vector<uint64_t> node_ids;
};

struct ReconvergenceInfo {
    bool is_reconvergent = false;
    bool is_hazardous = false;
    uint64_t common_source_id = 0;
    std::string common_source_name;
    std::string explanation;
};

struct Finding {
    std::string rule_id;
    std::string rule_name;
    std::string severity;
    uint64_t source_reg_id = 0;
    uint64_t dest_reg_id = 0;
    std::string source_reg_name;
    std::string dest_reg_name;
    std::string source_domain;
    std::string dest_domain;
    CrossingPath path;
    std::string reason;
    ir::SourceLoc source_loc;
    SyncPattern detected_sync = SyncPattern::None;
    ReconvergenceInfo reconvergence;
    bool waived = false;
    std::string waiver_justification;
    std::string waiver_owner;
    uint32_t bus_width = 1;
    bool is_gray_coded = false;
    bool has_handshake = false;
    bool has_multicycle_exception = false;
    int multicycle_cycles = 0;
    std::string constraint_source;
    std::string source_module_path;
    std::string dest_module_path;
    bool crosses_module_boundary = false;
};

class CrossingAnalyzer {
public:
    std::vector<Finding> analyze(
        const ir::Graph& graph,
        const std::vector<clock::ClockDomain>& domains,
        const std::unordered_map<uint64_t, size_t>& register_to_domain,
        size_t num_threads = 0);

    void set_pattern_recognizer(PatternRecognizer* recognizer) {
        pattern_recognizer_ = recognizer;
    }

    void set_clock_constraints(const clock::ClockConstraints* constraints) {
        clock_constraints_ = constraints;
    }

private:
    const clock::ClockDomain* find_domain_for_node(
        uint64_t node_id,
        const std::vector<clock::ClockDomain>& domains,
        const std::unordered_map<uint64_t, size_t>& register_to_domain) const;

    std::string build_reason(const Finding& f) const;

    bool is_safe_multi_bit_crossing(uint64_t src_id, uint64_t dst_id,
                                     const ir::Graph& graph) const;

    SynchronizerMatcher sync_matcher_;
    PatternRecognizer* pattern_recognizer_ = nullptr;
    const clock::ClockConstraints* clock_constraints_ = nullptr;
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_CROSSING_H
