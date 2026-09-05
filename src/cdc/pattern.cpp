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

    if (node->logic_type == ir::LogicType::GrayEncoder)
        return true;

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

                AsyncFifoPattern fifo;
                fifo.read_ptr_id = ptrs[i];
                fifo.write_ptr_id = ptrs[j];
                fifo.read_domain = a->clock_domain;
                fifo.write_domain = b->clock_domain;
                fifo.has_gray_encoding = a_gray && b_gray;
                fifo.verified = a_gray && b_gray;
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
                        // B3: Verified only for cross-domain handshake
                        hp.verified = (valid_node->clock_domain != ready_node->clock_domain);
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
            if (node.logic_type == ir::LogicType::GrayEncoder ||
                (node.logic_type == ir::LogicType::Xor &&
                 verify_gray_encoder_structure(node.id, graph))) {
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
            if (node.logic_type == ir::LogicType::GrayEncoder || node.is_gray_coded) {
                GrayCodePattern gp;
                gp.encoder_id = node.id;
                gp.decoder_id = 0;
                gp.verified = true;
                patterns.push_back(std::move(gp));
            }
            if (node.logic_type == ir::LogicType::GrayDecoder) {
                decoder_ids.push_back(node.id);
            }
        }
    }

    // Pair each decoder with the first unpaired encoder that directly drives it.
    for (uint64_t dec_id : decoder_ids) {
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

    // Explicit encoder→decoder logic-type pairs require a real connection.
    if (src->logic_type == ir::LogicType::GrayEncoder &&
        dst->logic_type == ir::LogicType::GrayDecoder && connected_to_dst(src_id)) {
        return true;
    }
    // NOTE: Only GrayEncoder(src)→GrayDecoder(dst) is a valid CDC-safe gray-code
    // crossing. The reversed direction (Decoder→Encoder) is NOT safe and is removed.

    // Gray-encoded source: safe when the source is a registered gray pattern
    // member. A pattern with a known decoder requires that decoder to be the
    // destination; a single-ended gray source (no decoder node in the IR —
    // decode happens downstream in the destination domain) is safe from the
    // encoder side.
    for (const auto& gp : gray_cache_) {
        if (gp.encoder_id != src_id)
            continue;
        if (gp.decoder_id == 0 || gp.decoder_id == dst_id)
            return true;
    }
    // Structural gray encoder feeding the source register (encoder output
    // registered before the crossing).
    for (const auto& gp : gray_cache_) {
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

    // Verified cross-domain handshake pair.
    for (const auto& handshake : handshake_cache_) {
        if ((handshake.valid_id == src_id && handshake.ready_id == dst_id) ||
            (handshake.ready_id == src_id && handshake.valid_id == dst_id)) {
            if (!handshake.verified)
                continue;

            // B3: Require a feedback path — the ready signal must have a register
            // successor in a different domain from itself (ack path back to source).
            // Without this, two cross-domain registers named "valid"/"ready" with
            // no actual handshake topology would pass verification.
            const ir::Node* ready_node = graph.find_node(handshake.ready_id);
            if (!ready_node)
                continue;
            bool has_feedback = false;
            for (uint64_t rsucc : graph.register_successors(handshake.ready_id, false)) {
                const ir::Node* succ = graph.find_node(rsucc);
                if (succ && succ->kind == ir::NodeKind::Register &&
                    succ->clock_domain != ready_node->clock_domain) {
                    has_feedback = true;
                    break;
                }
            }
            if (!has_feedback)
                continue;

            return true;
        }
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

}  // namespace opencdc::cdc
