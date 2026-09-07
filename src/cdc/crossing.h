#ifndef OPENCDC_CDC_CROSSING_H
#define OPENCDC_CDC_CROSSING_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "cdc/blackbox.h"
#include "cdc/pattern.h"
#include "cdc/synchronizer.h"
#include "clock/constraints.h"
#include "clock/domain.h"
#include "clock/relationship.h"
#include "config/config.h"
#include "ir/graph.h"

namespace opencdc::cdc {

enum class SafetyStatus { Unknown, Candidate, VerifiedSafe, VerifiedUnsafe, Ambiguous };

enum class MultiBitCrossingType {
    None,
    Raw,
    Synchronized,
    StaticData,
    HandshakeControlled,
    GrayCoded,
    AsyncFifo,
    Unknown
};

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
    std::string waiver_ticket;
    uint32_t bus_width = 1;
    bool is_gray_coded = false;
    bool has_handshake = false;
    bool has_multicycle_exception = false;
    int multicycle_cycles = 0;
    std::string constraint_source;
    std::string source_module_path;
    std::string dest_module_path;
    bool crosses_module_boundary = false;
    SafetyStatus safety_status = SafetyStatus::Unknown;
    std::string safety_provenance;
    bool suppressed_by_false_path = false;
    std::string false_path_source;
    bool suppressed_by_multicycle = false;
    std::string multicycle_source;
    clock::ClockRelationship clock_relationship = clock::ClockRelationship::Unknown;
    MultiBitCrossingType multi_bit_type = MultiBitCrossingType::None;
};

class CrossingAnalyzer {
   public:
    std::vector<Finding> analyze(const ir::Graph& graph,
                                 const std::vector<clock::ClockDomain>& domains,
                                 const std::unordered_map<uint64_t, size_t>& register_to_domain,
                                 size_t num_threads = 0);

    void set_pattern_recognizer(PatternRecognizer* recognizer) {
        pattern_recognizer_ = recognizer;
    }

    void set_clock_constraints(const clock::ClockConstraints* constraints) {
        clock_constraints_ = constraints;
    }

    void set_reset_policy(const config::ResetPolicyConfig* policy) {
        reset_policy_ = policy;
    }

    void set_multicycle_policy(const config::MulticyclePathPolicy* policy) {
        multicycle_policy_ = policy;
    }

    void set_resolve_result(const clock::ResolveResult* resolve) {
        resolve_result_ = resolve;
    }

    void set_blackbox_registry(const BlackBoxRegistry* registry) {
        blackbox_registry_ = registry;
    }

   private:
    const clock::ClockDomain* find_domain_for_node(
        uint64_t node_id, const std::vector<clock::ClockDomain>& domains,
        const std::unordered_map<uint64_t, size_t>& register_to_domain) const;

    std::string build_reason(const Finding& f) const;

    bool is_safe_multi_bit_crossing(uint64_t src_id, uint64_t dst_id, const ir::Graph& graph) const;

    bool is_path_through_safe_blackbox(const ir::Graph& graph,
                                       const std::vector<uint64_t>& path_node_ids) const;

    SynchronizerMatcher sync_matcher_;
    PatternRecognizer* pattern_recognizer_ = nullptr;
    const clock::ClockConstraints* clock_constraints_ = nullptr;
    const config::ResetPolicyConfig* reset_policy_ = nullptr;
    const config::MulticyclePathPolicy* multicycle_policy_ = nullptr;
    const clock::ResolveResult* resolve_result_ = nullptr;
    const BlackBoxRegistry* blackbox_registry_ = nullptr;
};

}  // namespace opencdc::cdc

#endif  // OPENCDC_CDC_CROSSING_H
