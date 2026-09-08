#include "cdc/pattern.h"

#include <algorithm>

namespace opencdc::cdc {

std::string PatternRecognizer::extract_base_name(const std::string& hier_name) const {
    size_t last_dot = hier_name.rfind('.');
    if (last_dot != std::string::npos) {
        return hier_name.substr(last_dot + 1);
    }
    return hier_name;
}

std::string PatternRecognizer::extract_module_name(const std::string& hier_name) const {
    size_t last_dot = hier_name.rfind('.');
    if (last_dot != std::string::npos) {
        if (last_dot == 0)
            return "";  // leading-dot name: no module component
        size_t second_last = hier_name.rfind('.', last_dot - 1);
        if (second_last != std::string::npos) {
            return hier_name.substr(second_last + 1, last_dot - second_last - 1);
        }
        return hier_name.substr(0, last_dot);
    }
    return "";
}

// A1: Structural-only — requires LogicType::GrayEncoder
bool PatternRecognizer::detect_xor_pattern(const ir::Node& node, const ir::Graph& graph) const {
    (void)graph;
    if (node.logic_type != ir::LogicType::Xor)
        return false;
    return node.logic_inputs.size() == 2;
}

bool PatternRecognizer::verify_gray_encoder_structure(uint64_t node_id,
                                                      const ir::Graph& graph) const {
    const ir::Node* node = graph.find_node(node_id);
    if (!node)
        return false;

    if (node->logic_type != ir::LogicType::Xor || node->logic_inputs.size() != 2)
        return false;

    uint64_t a_id = node->logic_inputs[0];
    uint64_t b_id = node->logic_inputs[1];
    const ir::Node* a = graph.find_node(a_id);
    const ir::Node* b = graph.find_node(b_id);
    if (!a || !b)
        return false;

    auto is_delay_of = [&](const ir::Node* src, uint64_t xor_other) -> bool {
        if (src->kind != ir::NodeKind::Register)
            return false;
        for (uint64_t pred : graph.predecessors(src->id)) {
            if (pred == xor_other)
                return true;
        }
        for (uint64_t reg_pred : graph.register_predecessors(src->id)) {
            if (reg_pred == xor_other)
                return true;
        }
        return false;
    };

    if (a->kind == ir::NodeKind::Register && is_delay_of(a, b_id))
        return true;
    if (b->kind == ir::NodeKind::Register && is_delay_of(b, a_id))
        return true;

    return false;
}

bool PatternRecognizer::detect_gray_encoder(const ir::Node& node, const ir::Graph& graph,
                                            std::vector<uint64_t>& inputs) const {
    (void)graph;
    if (node.logic_type == ir::LogicType::GrayEncoder) {
        inputs = node.logic_inputs;
        return true;
    }
    return false;
}

// A2: Structural-only — requires LogicType::GrayDecoder
bool PatternRecognizer::detect_gray_decoder(const ir::Node& node, const ir::Graph& graph,
                                            std::vector<uint64_t>& inputs) const {
    (void)graph;
    if (node.logic_type == ir::LogicType::GrayDecoder) {
        inputs = node.logic_inputs;
        return true;
    }
    return false;
}

// Keep module-name cross-reference — structural check (same instance)
// 4B: Gray decoder structural verification.
// A decoder must be driven by gray-coded input and produce binary output.
// Structural check: decoder node has GrayDecoder LogicType and is connected
// to an encoder (same width, same module).
bool PatternRecognizer::verify_gray_decoder_structure(uint64_t node_id,
                                                      const ir::Graph& graph) const {
    const ir::Node* node = graph.find_node(node_id);
    if (!node)
        return false;
    if (node->logic_type != ir::LogicType::GrayDecoder)
        return false;
    // Decoder must be combinational or register with matching width to encoder
    if (node->kind != ir::NodeKind::Combinational && node->kind != ir::NodeKind::Register)
        return false;
    return true;
}

// 4G: Check data stability: data registers driven by the source domain
// should not be modified by other paths while valid is asserted.
// Structural heuristic: data register's predecessors are all in the same
// module as the valid register (no external write path).
bool PatternRecognizer::check_data_stability(uint64_t valid_id, uint64_t ready_id,
                                             const ir::Graph& graph) const {
    const ir::Node* valid_node = graph.find_node(valid_id);
    if (!valid_node)
        return false;

    // Find data registers: successors of valid that are in a different domain
    // (data path registers).
    std::string valid_module = extract_module_name(valid_node->hier_name);
    if (valid_module.empty())
        return false;

    for (uint64_t data_succ : graph.register_successors(valid_id, false)) {
        const ir::Node* data_node = graph.find_node(data_succ);
        if (!data_node || data_node->kind != ir::NodeKind::Register)
            continue;
        if (data_node->clock_domain == valid_node->clock_domain)
            continue;

        // Check that data register's predecessors are all in the same module
        // (no external write path that could change data during valid).
        for (uint64_t pred : graph.register_predecessors(data_succ, false)) {
            const ir::Node* pred_node = graph.find_node(pred);
            if (!pred_node)
                continue;
            std::string pred_module = extract_module_name(pred_node->hier_name);
            if (pred_module != valid_module)
                return false;
        }
    }
    return true;
}

// 4H: Check acceptance gating: ready register should feed back to the
// valid register's domain (valid is deasserted when ready is asserted).
// Structural heuristic: ready register has a register successor in the
// valid register's domain.
bool PatternRecognizer::check_acceptance_gating(uint64_t valid_id, uint64_t ready_id,
                                                const ir::Graph& graph) const {
    const ir::Node* valid_node = graph.find_node(valid_id);
    const ir::Node* ready_node = graph.find_node(ready_id);
    if (!valid_node || !ready_node)
        return false;

    // Ready register must have a successor in the valid register's domain
    for (uint64_t rsucc : graph.register_successors(ready_id, false)) {
        const ir::Node* succ = graph.find_node(rsucc);
        if (succ && succ->kind == ir::NodeKind::Register &&
            succ->clock_domain == valid_node->clock_domain) {
            return true;
        }
    }
    return false;
}

// 4D: Detect full/empty flags for async FIFO.
// Full/empty flags are typically XOR/NXOR of read/write pointer bits.
// Structural check: register driven by XOR of both pointers (read and write).
bool PatternRecognizer::detect_fifo_full_empty(uint64_t ptr_id, const ir::Graph& graph,
                                               uint64_t& full_id, uint64_t& empty_id) const {
    full_id = 0;
    empty_id = 0;
    const ir::Node* ptr_node = graph.find_node(ptr_id);
    if (!ptr_node)
        return false;

    // Find combinational nodes driven by this pointer that also reference
    // a pointer in another domain (XOR comparison of read/write ptrs).
    for (uint64_t succ : graph.successors(ptr_id)) {
        const ir::Node* sn = graph.find_node(succ);
        if (!sn || sn->kind != ir::NodeKind::Combinational)
            continue;
        if (sn->logic_type != ir::LogicType::Xor)
            continue;
        // Check if this XOR has inputs from two different pointers
        // (read and write pointers being compared).
        if (sn->logic_inputs.size() != 2)
            continue;
        const ir::Node* a = graph.find_node(sn->logic_inputs[0]);
        const ir::Node* b = graph.find_node(sn->logic_inputs[1]);
        if (!a || !b)
            continue;
        if (a->kind != ir::NodeKind::Register || b->kind != ir::NodeKind::Register)
            continue;
        if (a->clock_domain == b->clock_domain)
            continue;
        // Found XOR of two cross-domain pointers: this is a full/empty comparison.
        // Find the register that captures this result.
        for (uint64_t xor_succ : graph.successors(succ)) {
            const ir::Node* flag_reg = graph.find_node(xor_succ);
            if (!flag_reg || flag_reg->kind != ir::NodeKind::Register)
                continue;
            // Classify as full or empty by name heuristic
            std::string base = extract_base_name(flag_reg->hier_name);
            std::string lower = base;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("full") != std::string::npos) {
                full_id = flag_reg->id;
            } else if (lower.find("empty") != std::string::npos) {
                empty_id = flag_reg->id;
            } else {
                // Ambiguous: could be full or empty
                if (!full_id)
                    full_id = flag_reg->id;
                else if (!empty_id)
                    empty_id = flag_reg->id;
            }
        }
    }
    return full_id != 0 || empty_id != 0;
}

// 4E: Detect memory element (RAM/register array) between write and read domains.
// Structural check: register with width > typical pointer width or with
// memory-like naming in the same module as the FIFO pointers.
bool PatternRecognizer::detect_fifo_memory(uint64_t write_ptr_id, uint64_t read_ptr_id,
                                           const std::string& module,
                                           const ir::Graph& graph) const {
    // Find registers in the same module that could be memory elements.
    // Memory elements are typically wider than the pointers or named
    // with memory-like patterns (ram, mem, buffer, array).
    const ir::Node* wr = graph.find_node(write_ptr_id);
    const ir::Node* rd = graph.find_node(read_ptr_id);
    if (!wr || !rd)
        return false;

    uint32_t ptr_width = std::max(wr->width, rd->width);

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        std::string node_mod = extract_module_name(node.hier_name);
        if (node_mod != module)
            continue;
        // Skip the pointers themselves
        if (node.id == write_ptr_id || node.id == read_ptr_id)
            continue;

        std::string base = extract_base_name(node.hier_name);
        std::string lower = base;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // Memory-like naming
        bool name_match =
            lower.find("ram") != std::string::npos || lower.find("mem") != std::string::npos ||
            lower.find("buffer") != std::string::npos || lower.find("array") != std::string::npos ||
            lower.find("fifo") != std::string::npos || lower.find("data") != std::string::npos;

        // Width-based: memory element is typically wider than pointers
        bool width_match = node.width > ptr_width;

        if (name_match || width_match)
            return true;
    }
    return false;
}

bool PatternRecognizer::detect_valid_ready_pair(uint64_t valid_id, uint64_t ready_id,
                                                const ir::Graph& graph) const {
    const ir::Node* valid_node = graph.find_node(valid_id);
    const ir::Node* ready_node = graph.find_node(ready_id);

    if (!valid_node || !ready_node)
        return false;

    std::string valid_mod = extract_module_name(valid_node->hier_name);
    std::string ready_mod = extract_module_name(ready_node->hier_name);

    return valid_mod == ready_mod;
}

static std::string lower_copy(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

static bool name_suggests_valid(const std::string& base_name) {
    std::string l = lower_copy(base_name);
    return l.find("valid") != std::string::npos;
}

static bool name_suggests_ready(const std::string& base_name) {
    std::string l = lower_copy(base_name);
    return l.find("ready") != std::string::npos || l.find("ack") != std::string::npos;
}

// A5 + B2: Structural-only — uses LogicType::AsyncFifoPtr / is_async_fifo_ptr flag
// Requires both pointers to have gray encoding
std::vector<AsyncFifoPattern> PatternRecognizer::detect_async_fifos(const ir::Graph& graph) const {
    std::vector<AsyncFifoPattern> fifos;

    // Collect all async FIFO pointer registers grouped by module.
    std::unordered_map<std::string, std::vector<uint64_t>> ptrs_by_module;
    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        if (!node.is_async_fifo_ptr && node.logic_type != ir::LogicType::AsyncFifoPtr)
            continue;
        std::string base = extract_module_name(node.hier_name);
        if (base.empty())
            continue;
        ptrs_by_module[base].push_back(node.id);
    }

    // Require graph connectivity: both pointers must be connected
    // (direct edge or shared successor — shared predecessor is too weak)
    auto are_connected = [&](uint64_t id_a, uint64_t id_b) -> bool {
        for (uint64_t sa : graph.successors(id_a)) {
            if (sa == id_b)
                return true;
        }
        for (uint64_t sb : graph.successors(id_b)) {
            if (sb == id_a)
                return true;
        }
        for (uint64_t sa : graph.register_successors(id_a)) {
            for (uint64_t sb : graph.register_successors(id_b)) {
                if (sa == sb)
                    return true;
            }
        }
        return false;
    };

    // For each module with 2+ async FIFO ptrs, try pairing across domains
    for (const auto& [mod, ptrs] : ptrs_by_module) {
        for (size_t i = 0; i < ptrs.size(); ++i) {
            for (size_t j = i + 1; j < ptrs.size(); ++j) {
                const ir::Node* a = graph.find_node(ptrs[i]);
                const ir::Node* b = graph.find_node(ptrs[j]);
                if (!a || !b)
                    continue;
                if (a->clock_domain == b->clock_domain)
                    continue;

                // Must have structural connectivity to same FIFO element
                if (!are_connected(ptrs[i], ptrs[j]))
                    continue;

                // Both must be gray-coded (structural requirement for async FIFO)
                bool a_gray = a->is_gray_coded || a->logic_type == ir::LogicType::GrayEncoder ||
                              a->logic_type == ir::LogicType::GrayDecoder;
                bool b_gray = b->is_gray_coded || b->logic_type == ir::LogicType::GrayEncoder ||
                              b->logic_type == ir::LogicType::GrayDecoder;

                // Check that each pointer has a register successor in the
                // other domain (synchronized pointer crossing).
                auto has_sync_in_other = [&](uint64_t ptr_id,
                                             const std::string& other_domain) -> bool {
                    for (uint64_t rsucc : graph.register_successors(ptr_id, false)) {
                        const ir::Node* succ = graph.find_node(rsucc);
                        if (succ && succ->kind == ir::NodeKind::Register &&
                            succ->clock_domain == other_domain) {
                            return true;
                        }
                    }
                    return false;
                };
                bool a_synced = has_sync_in_other(ptrs[i], b->clock_domain);
                bool b_synced = has_sync_in_other(ptrs[j], a->clock_domain);
                bool has_sync = a_synced || b_synced;

                AsyncFifoPattern fifo;
                fifo.read_ptr_id = ptrs[i];
                fifo.write_ptr_id = ptrs[j];
                fifo.read_domain = a->clock_domain;
                fifo.write_domain = b->clock_domain;
                fifo.has_gray_encoding = a_gray && b_gray;
                fifo.has_synchronized_ptr = has_sync;

                // 4D: Check for full/empty flags derived from pointer comparison
                uint64_t full_id = 0, empty_id = 0;
                fifo.has_full_empty = detect_fifo_full_empty(ptrs[i], graph, full_id, empty_id) ||
                                      detect_fifo_full_empty(ptrs[j], graph, full_id, empty_id);
                fifo.full_flag_id = full_id;
                fifo.empty_flag_id = empty_id;

                // 4E: Check for memory element between write and read domains
                fifo.has_memory = detect_fifo_memory(ptrs[i], ptrs[j], mod, graph);

                // Verified requires: gray encoding + synchronized pointer.
                // full/empty and memory are reported but not required for safety
                // (they improve confidence but their absence is not a safety issue).
                fifo.verified = a_gray && b_gray && has_sync;
                fifos.push_back(fifo);
            }
        }
    }

    return fifos;
}

// A4 + B3: Structural-only — uses LogicType::HandshakeValid/HandshakeReady /
// is_handshake_signal (role disambiguated by signal name).
// Verified only when valid and ready are in different clock domains
std::vector<HandshakePattern> PatternRecognizer::detect_handshakes(const ir::Graph& graph) const {
    std::vector<HandshakePattern> patterns;

    std::unordered_map<std::string, std::vector<uint64_t>> valid_signals;
    std::unordered_map<std::string, std::vector<uint64_t>> ready_signals;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;

        std::string base = extract_module_name(node.hier_name);

        if (node.logic_type == ir::LogicType::HandshakeValid) {
            valid_signals[base].push_back(node.id);
        } else if (node.logic_type == ir::LogicType::HandshakeReady) {
            ready_signals[base].push_back(node.id);
        } else if (node.is_handshake_signal) {
            // Flag without a LogicType role: classify by signal name. Names
            // that suggest neither role are ambiguous and skipped.
            std::string base_name = extract_base_name(node.hier_name);
            if (name_suggests_valid(base_name)) {
                valid_signals[base].push_back(node.id);
            } else if (name_suggests_ready(base_name)) {
                ready_signals[base].push_back(node.id);
            }
        }
    }

    for (const auto& [mod, valids] : valid_signals) {
        auto ready_it = ready_signals.find(mod);
        if (ready_it == ready_signals.end())
            continue;

        for (uint64_t valid_id : valids) {
            for (uint64_t ready_id : ready_it->second) {
                if (detect_valid_ready_pair(valid_id, ready_id, graph)) {
                    const ir::Node* valid_node = graph.find_node(valid_id);
                    const ir::Node* ready_node = graph.find_node(ready_id);

                    if (valid_node && ready_node) {
                        HandshakePattern hp;
                        hp.valid_id = valid_id;
                        hp.ready_id = ready_id;
                        hp.source_domain = valid_node->clock_domain;
                        hp.dest_domain = ready_node->clock_domain;

                        // Data path: valid register must drive a register in the
                        // destination domain (proves valid+data relationship).
                        hp.has_data_path = false;
                        for (uint64_t rsucc : graph.register_successors(valid_id, false)) {
                            const ir::Node* succ = graph.find_node(rsucc);
                            if (succ && succ->kind == ir::NodeKind::Register &&
                                succ->clock_domain == ready_node->clock_domain) {
                                hp.has_data_path = true;
                                break;
                            }
                        }

                        // Feedback path: ready register must drive a register
                        // in a different domain from itself (ack back to source).
                        hp.has_feedback_path = false;
                        for (uint64_t rsucc : graph.register_successors(ready_id, false)) {
                            const ir::Node* succ = graph.find_node(rsucc);
                            if (succ && succ->kind == ir::NodeKind::Register &&
                                succ->clock_domain != ready_node->clock_domain) {
                                hp.has_feedback_path = true;
                                break;
                            }
                        }

                        // 4G: Check data stability: data registers driven by
                        // valid should not have external write paths.
                        hp.has_data_stability = check_data_stability(valid_id, ready_id, graph);

                        // 4H: Check acceptance gating: ready feeds back to
                        // valid register's domain.
                        hp.has_acceptance_gating =
                            check_acceptance_gating(valid_id, ready_id, graph);

                        // B3: Verified only for cross-domain handshake with
                        // both data path and feedback path evidence.
                        hp.verified = (valid_node->clock_domain != ready_node->clock_domain) &&
                                      hp.has_data_path && hp.has_feedback_path;
                        patterns.push_back(hp);
                    }
                }
            }
        }
    }

    return patterns;
}

// Gray encoders are recognized on combinational nodes (XOR-of-delayed-register
// structure or explicit GrayEncoder logic type) and on registers (explicit
// GrayEncoder logic type or the frontend's is_gray_coded flag). Decoders are
// paired with encoders only through a real graph connection.
std::vector<GrayCodePattern> PatternRecognizer::detect_gray_encoding(const ir::Graph& graph) const {
    std::vector<GrayCodePattern> patterns;  // encoders; decoder_id == 0 until paired
    std::vector<uint64_t> decoder_ids;

    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Combinational) {
            bool structural_match = (node.logic_type == ir::LogicType::Xor &&
                                     verify_gray_encoder_structure(node.id, graph));
            if (structural_match || node.logic_type == ir::LogicType::GrayEncoder) {
                GrayCodePattern gp;
                gp.encoder_id = node.id;
                gp.decoder_id = 0;
                gp.verified = true;
                gp.data_path = node.logic_inputs;
                patterns.push_back(std::move(gp));
            } else if (node.logic_type == ir::LogicType::GrayDecoder) {
                decoder_ids.push_back(node.id);
            }
        } else if (node.kind == ir::NodeKind::Register) {
            if (node.is_gray_coded) {
                GrayCodePattern gp;
                gp.encoder_id = node.id;
                gp.decoder_id = 0;
                gp.verified = true;
                patterns.push_back(std::move(gp));
            } else if (node.logic_type == ir::LogicType::GrayEncoder) {
                // Frontend labels this as a gray encoder but the node lacks
                // the is_gray_coded flag (set by AST analysis of the
                // gray <= bin ^ (bin >> 1) pattern).  Detected but not verified.
                GrayCodePattern gp;
                gp.encoder_id = node.id;
                gp.decoder_id = 0;
                gp.verified = false;
                patterns.push_back(std::move(gp));
            }
            if (node.logic_type == ir::LogicType::GrayDecoder) {
                decoder_ids.push_back(node.id);
            }
        }
    }

    // Pair each decoder with the first unpaired encoder that directly drives it.
    // 4B: Decoder must also be structurally verified.
    for (uint64_t dec_id : decoder_ids) {
        if (!verify_gray_decoder_structure(dec_id, graph))
            continue;
        for (auto& gp : patterns) {
            if (gp.decoder_id != 0)
                continue;
            bool connected = false;
            for (uint64_t succ : graph.successors(gp.encoder_id)) {
                if (succ == dec_id) {
                    connected = true;
                    break;
                }
            }
            if (connected) {
                gp.decoder_id = dec_id;
                gp.has_structural_proof = true;
                break;
            }
        }
    }

    return patterns;
}

bool PatternRecognizer::is_gray_coded(uint64_t node_id, const ir::Graph& graph) const {
    const ir::Node* node = graph.find_node(node_id);
    if (!node)
        return false;
    return node->is_gray_coded || node->logic_type == ir::LogicType::GrayEncoder ||
           node->logic_type == ir::LogicType::GrayDecoder;
}

bool PatternRecognizer::is_handshake_signal(uint64_t node_id, const ir::Graph& graph) const {
    const ir::Node* node = graph.find_node(node_id);
    if (!node)
        return false;
    return node->is_handshake_signal || node->logic_type == ir::LogicType::HandshakeValid ||
           node->logic_type == ir::LogicType::HandshakeReady;
}

bool PatternRecognizer::is_async_fifo_ptr(uint64_t node_id, const ir::Graph& graph) const {
    const ir::Node* node = graph.find_node(node_id);
    if (!node)
        return false;
    return node->is_async_fifo_ptr || node->logic_type == ir::LogicType::AsyncFifoPtr;
}

bool PatternRecognizer::is_pulse_sync(uint64_t node_id, const ir::Graph& graph) const {
    const ir::Node* node = graph.find_node(node_id);
    if (!node)
        return false;
    for (const auto& ps : pulse_cache_) {
        if (ps.source_reg_id == node_id || ps.edge_detector_id == node_id ||
            ps.first_sync_stage_id == node_id) {
            return true;
        }
    }
    return false;
}

bool PatternRecognizer::is_toggle_sync(uint64_t node_id, const ir::Graph& graph) const {
    const ir::Node* node = graph.find_node(node_id);
    if (!node)
        return false;
    for (const auto& ts : toggle_cache_) {
        if (ts.source_reg_id == node_id || ts.toggle_ff_id == node_id ||
            ts.first_sync_stage_id == node_id) {
            return true;
        }
    }
    return false;
}

// 3A: Pulse synchronizer detection.
// Pattern: register R → XOR(R, R_delayed) → 2FF sync chain in another domain.
// The XOR node is an edge detector producing a one-cycle pulse on each toggle.
std::vector<PulseSyncPattern> PatternRecognizer::detect_pulse_sync(const ir::Graph& graph) const {
    std::vector<PulseSyncPattern> patterns;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Combinational)
            continue;
        if (node.logic_type != ir::LogicType::Xor || node.logic_inputs.size() != 2)
            continue;

        // Check if this XOR is an edge detector: one input is a register, the
        // other input is the same register (feedback — register-delayed copy).
        uint64_t a_id = node.logic_inputs[0];
        uint64_t b_id = node.logic_inputs[1];
        const ir::Node* a = graph.find_node(a_id);
        const ir::Node* b = graph.find_node(b_id);
        if (!a || !b)
            continue;

        const ir::Node* reg_node = nullptr;
        if (a->kind == ir::NodeKind::Register && b_id == a_id) {
            // Both inputs are the same register — XOR with self = always 0,
            // not an edge detector. Skip.
            continue;
        }
        if (a->kind == ir::NodeKind::Register && b->kind == ir::NodeKind::Register) {
            // Two different registers: check if one feeds the other (delayed).
            // Edge detector pattern: XOR(R, R_delayed) where R_delayed is R
            // registered through another FF. We check structural connectivity:
            // does register 'a' have a register predecessor that is 'b', or vice versa?
            auto is_delay_chain = [&](uint64_t src, uint64_t dst) -> bool {
                for (uint64_t rp : graph.register_predecessors(dst, false)) {
                    if (rp == src)
                        return true;
                }
                return false;
            };
            if (is_delay_chain(a_id, b_id)) {
                reg_node = a;
            } else if (is_delay_chain(b_id, a_id)) {
                reg_node = b;
            } else {
                continue;
            }
        } else if (a->kind == ir::NodeKind::Register) {
            reg_node = a;
        } else if (b->kind == ir::NodeKind::Register) {
            reg_node = b;
        } else {
            continue;
        }

        if (!reg_node)
            continue;

        // The XOR output must feed into a 2FF synchronizer chain in a
        // different clock domain.
        for (uint64_t xor_succ : graph.register_successors(node.id, false)) {
            const ir::Node* first_stage = graph.find_node(xor_succ);
            if (!first_stage || first_stage->kind != ir::NodeKind::Register)
                continue;
            if (first_stage->clock_domain == reg_node->clock_domain)
                continue;

            // Check for 2FF chain: first_stage → second_stage in same domain
            bool has_second_stage = false;
            for (uint64_t fs_succ : graph.register_successors(xor_succ, false)) {
                const ir::Node* second_stage = graph.find_node(fs_succ);
                if (second_stage && second_stage->kind == ir::NodeKind::Register &&
                    second_stage->clock_domain == first_stage->clock_domain) {
                    has_second_stage = true;
                    break;
                }
            }
            if (!has_second_stage)
                continue;

            PulseSyncPattern ps;
            ps.source_reg_id = reg_node->id;
            ps.edge_detector_id = node.id;
            ps.first_sync_stage_id = first_stage->id;
            ps.source_domain = reg_node->clock_domain;
            ps.dest_domain = first_stage->clock_domain;
            ps.verified = true;
            patterns.push_back(std::move(ps));
            break;  // one pattern per edge detector is enough
        }
    }

    return patterns;
}

// 3B: Toggle synchronizer detection.
// Pattern: register T with toggle feedback (T XOR 1 or ~T) → 2FF sync chain
// in another domain. The toggle register alternates each source clock edge.
std::vector<ToggleSyncPattern> PatternRecognizer::detect_toggle_sync(const ir::Graph& graph) const {
    std::vector<ToggleSyncPattern> patterns;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;

        // Check if this register has toggle feedback: one of its combinational
        // predecessors is an XOR or NOT that takes this register as input.
        bool has_toggle_feedback = false;
        for (uint64_t pred : graph.predecessors(node.id)) {
            const ir::Node* pn = graph.find_node(pred);
            if (!pn || pn->kind != ir::NodeKind::Combinational)
                continue;

            if (pn->logic_type == ir::LogicType::Not) {
                // NOT gate with this register as input: T <= ~T
                for (uint64_t not_input : pn->logic_inputs) {
                    if (not_input == node.id) {
                        has_toggle_feedback = true;
                        break;
                    }
                }
            } else if (pn->logic_type == ir::LogicType::Xor) {
                // XOR with this register and constant 1 (or another register
                // that is constant 1). Structural check: one input is this
                // register, the other is a constant or different register.
                if (pn->logic_inputs.size() == 2) {
                    uint64_t x0 = pn->logic_inputs[0];
                    uint64_t x1 = pn->logic_inputs[1];
                    if ((x0 == node.id && x1 != node.id) || (x1 == node.id && x0 != node.id)) {
                        has_toggle_feedback = true;
                        break;
                    }
                }
            }
            if (has_toggle_feedback)
                break;
        }
        if (!has_toggle_feedback)
            continue;

        // The toggle register must feed a 2FF synchronizer chain in a
        // different clock domain.
        for (uint64_t t_succ : graph.register_successors(node.id, false)) {
            const ir::Node* first_stage = graph.find_node(t_succ);
            if (!first_stage || first_stage->kind != ir::NodeKind::Register)
                continue;
            if (first_stage->clock_domain == node.clock_domain)
                continue;

            // Check for 2FF chain
            bool has_second_stage = false;
            for (uint64_t fs_succ : graph.register_successors(t_succ, false)) {
                const ir::Node* second_stage = graph.find_node(fs_succ);
                if (second_stage && second_stage->kind == ir::NodeKind::Register &&
                    second_stage->clock_domain == first_stage->clock_domain) {
                    has_second_stage = true;
                    break;
                }
            }
            if (!has_second_stage)
                continue;

            ToggleSyncPattern ts;
            ts.source_reg_id = node.id;
            ts.toggle_ff_id = node.id;
            ts.first_sync_stage_id = first_stage->id;
            ts.source_domain = node.clock_domain;
            ts.dest_domain = first_stage->clock_domain;
            ts.verified = true;
            patterns.push_back(std::move(ts));
            break;  // one pattern per toggle register
        }
    }

    return patterns;
}

void PatternRecognizer::ensure_patterns(const ir::Graph& graph) const {
    std::lock_guard<std::mutex> lock(pattern_mutex_);
    ensure_patterns_locked(graph);
}

void PatternRecognizer::ensure_patterns_locked(const ir::Graph& graph) const {
    // Caller must hold pattern_mutex_.
    if (cached_graph_generation_ == graph.generation())
        return;
    fifo_cache_ = detect_async_fifos(graph);
    handshake_cache_ = detect_handshakes(graph);
    gray_cache_ = detect_gray_encoding(graph);
    pulse_cache_ = detect_pulse_sync(graph);
    toggle_cache_ = detect_toggle_sync(graph);
    cached_graph_generation_ = graph.generation();
}

// B1: Requires structural verification of the specific crossing — a bare
// is_gray_coded flag or unrelated same-type nodes are NOT sufficient.
bool PatternRecognizer::is_verified_safe_crossing(uint64_t src_id, uint64_t dst_id,
                                                  const ir::Graph& graph) const {
    const ir::Node* src = graph.find_node(src_id);
    const ir::Node* dst = graph.find_node(dst_id);
    if (!src || !dst)
        return false;

    std::lock_guard<std::mutex> lock(pattern_mutex_);
    ensure_patterns_locked(graph);

    auto connected_to_dst = [&](uint64_t from_id) -> bool {
        for (uint64_t succ : graph.successors(from_id)) {
            if (succ == dst_id)
                return true;
        }
        for (uint64_t rsucc : graph.register_successors(from_id, false)) {
            if (rsucc == dst_id)
                return true;
        }
        return false;
    };

    // Explicit encoder→decoder logic-type pairs require a real connection AND
    // structural verification of the encoder (XOR-of-delayed-register pattern).
    if (src->logic_type == ir::LogicType::GrayEncoder &&
        dst->logic_type == ir::LogicType::GrayDecoder && connected_to_dst(src_id) &&
        verify_gray_encoder_structure(src_id, graph)) {
        return true;
    }
    // NOTE: Only GrayEncoder(src)→GrayDecoder(dst) with structural proof is a
    // valid CDC-safe gray-code crossing.  The reversed direction
    // (Decoder→Encoder) is NOT safe.  Bare LogicType::GrayEncoder without XOR
    // structure is NOT sufficient.

    // Gray-encoded source: safe only when the source is a registered gray
    // pattern member with structural verification (gp.verified == true).  A
    // pattern with a known decoder requires that decoder to be the
    // destination; a single-ended gray source (no decoder node in the IR —
    // decode happens downstream in the destination domain) is safe from the
    // encoder side when the encoder has structural proof.
    for (const auto& gp : gray_cache_) {
        if (gp.encoder_id != src_id)
            continue;
        if (!gp.verified)
            continue;
        if (gp.decoder_id == 0 || gp.decoder_id == dst_id)
            return true;
    }
    // Structural gray encoder feeding the source register (encoder output
    // registered before the crossing).  The encoder must be structurally
    // verified (XOR-of-delayed-register pattern or is_gray_coded flag).
    for (const auto& gp : gray_cache_) {
        if (!gp.verified)
            continue;
        if (gp.decoder_id != 0)
            continue;
        for (uint64_t pred : graph.predecessors(src_id)) {
            if (pred == gp.encoder_id)
                return true;
        }
    }

    // Async FIFO pointer pair: safe only when both endpoints are members of a
    // verified (gray-encoded) FIFO pattern.
    for (const auto& fifo : fifo_cache_) {
        if ((fifo.read_ptr_id == src_id && fifo.write_ptr_id == dst_id) ||
            (fifo.write_ptr_id == src_id && fifo.read_ptr_id == dst_id)) {
            return fifo.verified;
        }
    }

    // Verified cross-domain handshake pair with data path and feedback path.
    for (const auto& handshake : handshake_cache_) {
        if ((handshake.valid_id == src_id && handshake.ready_id == dst_id) ||
            (handshake.ready_id == src_id && handshake.valid_id == dst_id)) {
            if (!handshake.verified)
                continue;
            return true;
        }
    }

    // 3A: Pulse synchronizer pattern: source register → edge detector (XOR) →
    // 2FF chain in destination domain.  The edge detector output is the
    // crossing, and the source register is a verified safe crossing source.
    for (const auto& ps : pulse_cache_) {
        if (!ps.verified)
            continue;
        if (ps.source_reg_id == src_id && ps.first_sync_stage_id == dst_id)
            return true;
        if (ps.edge_detector_id == src_id && ps.first_sync_stage_id == dst_id)
            return true;
    }

    // 3B: Toggle synchronizer pattern: toggle register → 2FF chain in
    // destination domain.
    for (const auto& ts : toggle_cache_) {
        if (!ts.verified)
            continue;
        if (ts.toggle_ff_id == src_id && ts.first_sync_stage_id == dst_id)
            return true;
    }

    return false;
}

void PatternRecognizer::analyze_and_annotate(ir::Graph& graph) {
    {
        std::lock_guard<std::mutex> lock(pattern_mutex_);
        ensure_patterns_locked(graph);
    }

    // Propagate detected patterns onto node flags. OR with existing flags so
    // frontend-provided annotations survive.
    for (auto& node : graph.nodes_mutable()) {
        if (!node.is_gray_coded) {
            bool gray = node.logic_type == ir::LogicType::GrayEncoder ||
                        node.logic_type == ir::LogicType::GrayDecoder;
            for (const auto& gp : gray_cache_) {
                if (gp.encoder_id == node.id || gp.decoder_id == node.id) {
                    gray = true;
                    break;
                }
            }
            node.is_gray_coded = gray;
        }

        if (!node.is_handshake_signal) {
            bool hs = node.logic_type == ir::LogicType::HandshakeValid ||
                      node.logic_type == ir::LogicType::HandshakeReady;
            for (const auto& hp : handshake_cache_) {
                if (hp.valid_id == node.id || hp.ready_id == node.id) {
                    hs = true;
                    break;
                }
            }
            node.is_handshake_signal = hs;
        }

        if (!node.is_async_fifo_ptr) {
            bool fifo = node.logic_type == ir::LogicType::AsyncFifoPtr;
            for (const auto& fp : fifo_cache_) {
                if (fp.read_ptr_id == node.id || fp.write_ptr_id == node.id) {
                    fifo = true;
                    break;
                }
            }
            node.is_async_fifo_ptr = fifo;
        }
    }
}

bool PatternRecognizer::verify_async_fifo(uint64_t src_id, uint64_t dst_id,
                                           const ir::Graph& graph,
                                           std::string& failure_reason) const {
    std::lock_guard<std::mutex> lock(pattern_mutex_);
    ensure_patterns_locked(graph);

    for (auto& fifo : fifo_cache_) {
        bool match =
            (fifo.read_ptr_id == src_id && fifo.write_ptr_id == dst_id) ||
            (fifo.write_ptr_id == src_id && fifo.read_ptr_id == dst_id) ||
            (fifo.read_ptr_id == src_id || fifo.write_ptr_id == src_id ||
             fifo.read_ptr_id == dst_id || fifo.write_ptr_id == dst_id);

        if (!match)
            continue;

        fifo.gray_encoded_wptr = fifo.has_gray_encoding;
        fifo.gray_encoded_rptr = fifo.has_gray_encoding;

        // Check sync chain on write pointer side.
        fifo.sync_chain_on_wptr = false;
        for (uint64_t succ : graph.register_successors(fifo.write_ptr_id)) {
            const ir::Node* n = graph.find_node(succ);
            if (n && n->kind == ir::NodeKind::Register && n->clock_domain == fifo.read_domain) {
                fifo.sync_chain_on_wptr = true;
                break;
            }
        }

        // Check sync chain on read pointer side.
        fifo.sync_chain_on_rptr = false;
        for (uint64_t succ : graph.register_successors(fifo.read_ptr_id)) {
            const ir::Node* n = graph.find_node(succ);
            if (n && n->kind == ir::NodeKind::Register && n->clock_domain == fifo.write_domain) {
                fifo.sync_chain_on_rptr = true;
                break;
            }
        }

        fifo.full_empty_flags_present = fifo.has_full_empty;
        fifo.memory_between_domains = fifo.has_memory;

        // Build failure reason.
        std::vector<std::string> failures;
        if (!fifo.gray_encoded_wptr)
            failures.push_back("write pointer not gray-encoded");
        if (!fifo.gray_encoded_rptr)
            failures.push_back("read pointer not gray-encoded");
        if (!fifo.sync_chain_on_wptr)
            failures.push_back("no sync chain on write pointer");
        if (!fifo.sync_chain_on_rptr)
            failures.push_back("no sync chain on read pointer");
        if (!fifo.full_empty_flags_present)
            failures.push_back("no full/empty flags");
        if (!fifo.memory_between_domains)
            failures.push_back("no memory between domains");

        fifo.verified = failures.empty();
        if (!failures.empty()) {
            failure_reason = "Async FIFO verification failed: ";
            for (size_t i = 0; i < failures.size(); ++i) {
                if (i > 0)
                    failure_reason += "; ";
                failure_reason += failures[i];
            }
        }
        return fifo.verified;
    }

    failure_reason = "No async FIFO pattern found for given source/destination";
    return false;
}

}  // namespace opencdc::cdc
