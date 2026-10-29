#include "ir/graph.h"

#include <algorithm>
#include <deque>
#include <unordered_set>

namespace opencdc::ir {

static std::string leaf_name(const std::string& hier_name) {
    size_t dot = hier_name.rfind('.');
    if (dot == std::string::npos)
        return hier_name;
    return hier_name.substr(dot + 1);
}

void Graph::set_limits(size_t max_nodes, size_t max_edges) {
    // Node IDs feed a 32-bit edge-key pack; keep the same bound the
    // static_assert enforces for the default constant.
    if (max_nodes > 0) {
        max_nodes_ = max_nodes > UINT32_MAX ? UINT32_MAX : max_nodes;
    }
    if (max_edges > 0)
        max_edges_ = max_edges;
}

uint64_t Graph::add_register(const std::string& hier_name, const std::string& clock_domain,
                             uint32_t width, const SourceLoc& loc, const std::string& module_path,
                             const std::string& module_type) {
    // Return existing node if hier_name already present — prevents orphaned
    // nodes when multiple always blocks assign to the same register.
    // Update empty fields so re-declaration with clock info wins over placeholders.
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        Node& n = nodes_[existing->second];
        if (n.clock_domain.empty() && !clock_domain.empty())
            n.clock_domain = clock_domain;
        if (n.module_path.empty() && !module_path.empty())
            n.module_path = module_path;
        if (n.module_type.empty() && !module_type.empty())
            n.module_type = module_type;
        if (n.width == 1 && width != 1)
            n.width = width;
        ++generation_;
        dirty_ = true;
        return n.id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Register;
    node.width = width;
    node.clock_domain = clock_domain;
    node.loc = loc;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;
    return id;
}

uint64_t Graph::add_port(const std::string& hier_name, uint32_t width, const SourceLoc& loc,
                         const std::string& module_path, const std::string& module_type) {
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        return nodes_[existing->second].id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Port;
    node.width = width;
    node.loc = loc;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;
    return id;
}

uint64_t Graph::add_net(const std::string& hier_name, uint32_t width, const SourceLoc& loc,
                        const std::string& module_path, const std::string& module_type) {
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        return nodes_[existing->second].id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Net;
    node.width = width;
    node.loc = loc;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;
    return id;
}

uint64_t Graph::add_combinational(const std::string& hier_name, LogicType logic_type,
                                  const std::vector<uint64_t>& inputs, uint32_t width,
                                  const SourceLoc& loc, const std::string& module_path,
                                  const std::string& module_type) {
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        return nodes_[existing->second].id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Combinational;
    node.width = width;
    node.loc = loc;
    node.logic_type = logic_type;
    node.logic_inputs = inputs;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;

    for (uint64_t input_id : inputs) {
        add_edge(input_id, id);
    }

    return id;
}

void Graph::add_edge(uint64_t from_id, uint64_t to_id) {
    add_edge(from_id, to_id, EdgeRole::Data);
}

void Graph::add_edge(uint64_t from_id, uint64_t to_id, EdgeRole role) {
    if (from_id == to_id)
        return;  // no self-loops
    if (!find_node(from_id) || !find_node(to_id))
        return;
    if (edges_.size() >= max_edges_) {
        truncated_ = true;
        return;
    }

    // O(1) dedup using packed edge key instead of O(N) std::find.
    // IDs beyond 32 bits cannot pack safely — fall back to linear check.
    if (from_id > UINT32_MAX || to_id > UINT32_MAX) {
        for (const auto& e : edges_) {
            if (e.from_id == from_id && e.to_id == to_id)
                return;
        }
    } else {
        uint64_t edge_key = (from_id << 32) | to_id;
        if (!edge_set_.insert(edge_key).second)
            return;
    }

    edges_.push_back({from_id, to_id, role});
    adj_[from_id].push_back(to_id);
    radj_[to_id].push_back(from_id);
    ++generation_;
    dirty_ = true;
}

const Node* Graph::find_node(uint64_t id) const {
    auto it = id_to_idx_.find(id);
    if (it == id_to_idx_.end())
        return nullptr;
    return &nodes_[it->second];
}

void Graph::remove_nodes(const std::vector<uint64_t>& dead_ids) {
    if (dead_ids.empty())
        return;
    std::unordered_set<uint64_t> dead(dead_ids.begin(), dead_ids.end());

    // Drop dead nodes; surviving node ids are unchanged (only their vector
    // index shifts), so every id reference in Node fields stays valid.
    std::vector<Node> kept;
    kept.reserve(nodes_.size());
    for (auto& n : nodes_) {
        if (dead.count(n.id))
            continue;
        kept.push_back(std::move(n));
    }
    nodes_ = std::move(kept);

    // Rebuild node indices.
    id_to_idx_.clear();
    name_to_idx_.clear();
    id_to_idx_.reserve(nodes_.size());
    name_to_idx_.reserve(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        id_to_idx_[nodes_[i].id] = i;
        name_to_idx_[nodes_[i].hier_name] = i;
    }

    // Drop edges incident to dead nodes and rebuild adjacency + dedup set.
    std::vector<Edge> kept_edges;
    kept_edges.reserve(edges_.size());
    for (const auto& e : edges_) {
        if (dead.count(e.from_id) || dead.count(e.to_id))
            continue;
        kept_edges.push_back(e);
    }
    edges_ = std::move(kept_edges);
    adj_.clear();
    radj_.clear();
    edge_set_.clear();
    for (const auto& e : edges_) {
        adj_[e.from_id].push_back(e.to_id);
        radj_[e.to_id].push_back(e.from_id);
        if (e.from_id <= UINT32_MAX && e.to_id <= UINT32_MAX)
            edge_set_.insert((e.from_id << 32) | e.to_id);
    }
    mark_dirty();
}

Node* Graph::find_node_mutable(uint64_t id) {
    auto it = id_to_idx_.find(id);
    if (it == id_to_idx_.end())
        return nullptr;
    return &nodes_[it->second];
}

const Node* Graph::find_node_by_name(const std::string& hier_name) const {
    auto it = name_to_idx_.find(hier_name);
    if (it == name_to_idx_.end())
        return nullptr;
    return &nodes_[it->second];
}

std::vector<uint64_t> Graph::successors(uint64_t id) const {
    auto it = adj_.find(id);
    if (it == adj_.end())
        return {};
    return it->second;
}

std::vector<uint64_t> Graph::predecessors(uint64_t id) const {
    auto it = radj_.find(id);
    if (it == radj_.end())
        return {};
    return it->second;
}

size_t Graph::register_count() const {
    return std::count_if(nodes_.begin(), nodes_.end(),
                         [](const Node& n) { return n.kind == NodeKind::Register; });
}

std::vector<uint64_t> Graph::register_predecessors(uint64_t id, bool traverse_combinational) const {
    std::vector<uint64_t> result;
    std::vector<uint64_t> stack = predecessors(id);
    std::unordered_set<uint64_t> visited;
    visited.insert(id);

    while (!stack.empty()) {
        uint64_t cur = stack.back();
        stack.pop_back();
        if (visited.count(cur))
            continue;
        visited.insert(cur);

        const Node* n = find_node(cur);
        if (!n)
            continue;

        if (n->kind == NodeKind::Register) {
            result.push_back(cur);
        } else if (is_data_node(n->kind) &&
                   (traverse_combinational || n->kind != NodeKind::Combinational)) {
            auto preds = predecessors(cur);
            for (uint64_t p : preds) {
                if (!visited.count(p))
                    stack.push_back(p);
            }
        }
    }
    return result;
}

std::vector<uint64_t> Graph::register_successors(uint64_t id, bool traverse_combinational) const {
    std::vector<uint64_t> result;
    std::vector<uint64_t> stack = successors(id);
    std::unordered_set<uint64_t> visited;
    visited.insert(id);

    while (!stack.empty()) {
        uint64_t cur = stack.back();
        stack.pop_back();
        if (visited.count(cur))
            continue;
        visited.insert(cur);

        const Node* n = find_node(cur);
        if (!n)
            continue;

        if (n->kind == NodeKind::Register) {
            result.push_back(cur);
        } else if (is_data_node(n->kind) &&
                   (traverse_combinational || n->kind != NodeKind::Combinational)) {
            auto succs = successors(cur);
            for (uint64_t s : succs) {
                if (!visited.count(s))
                    stack.push_back(s);
            }
        }
    }
    return result;
}

Graph::PathTraversalResult Graph::find_register_paths(uint64_t src_reg_id, size_t max_paths) const {
    PathTraversalResult result;
    result.max_paths = max_paths;

    // Rope-style traversal: each Frame carries an O(1) hash-set for cycle
    // detection instead of an O(D) vector scan.  The full path is materialised
    // only at register-to-register endpoints by walking a compact parent-pointer
    // array (rope), so we avoid copying the whole path vector per frame.
    struct RopeNode {
        uint64_t node_id;
        int parent_idx;  // index into rope[], -1 for root
    };
    std::vector<RopeNode> rope;
    rope.push_back({src_reg_id, -1});

    struct Frame {
        uint64_t node_id;
        int rope_idx;  // index of this node in rope[]
        bool has_comb;
        std::unordered_set<uint64_t> on_path;  // O(1) cycle check
    };

    // Breadth-first: with each node expanded once, the recorded path to a
    // destination is a shortest one, and MAX_PATH_DEPTH bounds its length.
    std::deque<Frame> stack;
    {
        std::unordered_set<uint64_t> on_path;
        on_path.insert(src_reg_id);
        stack.push_back({src_reg_id, 0, false, std::move(on_path)});
    }
    result.visited_nodes = 1;

    // Local per-call edge tracking (not a member — must stay local)
    std::unordered_set<uint64_t> visited_edges;
    // Every consumer keeps one path per destination register, so each data
    // node is expanded once and each destination recorded once: enumerating
    // every simple path is exponential in the combinational fan-in/fan-out
    // (a CPU core hit max_paths from one register).
    std::unordered_set<uint64_t> expanded;
    std::unordered_set<uint64_t> reached;

    while (!stack.empty()) {
        Frame f = std::move(stack.front());
        stack.pop_front();
        if (!expanded.insert(f.node_id).second)
            continue;

        // E3: Depth limit (on_path.size() == number of nodes on current path)
        if (f.on_path.size() > MAX_PATH_DEPTH) {
            result.truncated = true;
            result.truncated_by_depth = true;
            continue;
        }

        for (uint64_t succ : successors(f.node_id)) {
            if (f.on_path.count(succ))
                continue;  // O(1) cycle check

            result.visited_nodes++;
            const Node* sn = find_node(succ);
            if (!sn)
                continue;

            // Add successor to rope
            int succ_rope_idx = static_cast<int>(rope.size());
            rope.push_back({succ, f.rope_idx});

            uint64_t edge_key =
                (static_cast<uint64_t>(f.node_id) << 32) | static_cast<uint64_t>(succ);
            if (visited_edges.insert(edge_key).second) {
                result.visited_edges++;
            }

            if (sn->kind == NodeKind::Register) {
                if (!reached.insert(succ).second)
                    continue;
                // Materialise path only at register endpoints
                RegPath rp;
                rp.src_reg_id = src_reg_id;
                rp.dst_reg_id = succ;
                rp.has_combinational = f.has_comb;
                // Walk rope backward to build node_ids in order
                rp.node_ids.reserve(f.on_path.size() + 1);
                int idx = succ_rope_idx;
                while (idx >= 0) {
                    rp.node_ids.push_back(rope[static_cast<size_t>(idx)].node_id);
                    idx = rope[static_cast<size_t>(idx)].parent_idx;
                }
                std::reverse(rp.node_ids.begin(), rp.node_ids.end());
                result.paths.push_back(std::move(rp));
                if (result.paths.size() >= max_paths) {
                    result.truncated = true;
                    return result;
                }
            } else if (is_data_node(sn->kind) && !expanded.count(succ)) {
                Frame nf;
                nf.node_id = succ;
                nf.rope_idx = succ_rope_idx;
                nf.has_comb = f.has_comb || (sn->kind == NodeKind::Combinational);
                nf.on_path = f.on_path;  // copy the small set
                nf.on_path.insert(succ);
                stack.push_back(std::move(nf));
            }
        }
    }

    return result;
}

ValidationResult Graph::validate() const {
    ValidationResult result;

    for (const auto& [id, index] : id_to_idx_) {
        if (index >= nodes_.size() || nodes_[index].id != id) {
            result.errors.push_back("Invalid node index for id " + std::to_string(id));
        }
    }

    std::unordered_set<std::string> names;
    for (const auto& node : nodes_) {
        if (node.width == 0) {
            result.errors.push_back("Node '" + node.hier_name + "' has zero width");
        }
        if (!names.insert(node.hier_name).second) {
            result.errors.push_back("Duplicate node name '" + node.hier_name + "'");
        }
    }

    for (const auto& edge : edges_) {
        if (!find_node(edge.from_id) || !find_node(edge.to_id)) {
            result.errors.push_back("Dangling edge " + std::to_string(edge.from_id) + " -> " +
                                    std::to_string(edge.to_id));
        }
    }

    return result;
}

}  // namespace opencdc::ir
