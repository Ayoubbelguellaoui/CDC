#include "ir/module_tree.h"

#include <algorithm>
#include <unordered_set>

namespace opencdc::ir {

static std::string parent_module(const std::string& hier_name) {
    size_t last_dot = hier_name.rfind('.');
    if (last_dot == std::string::npos || last_dot == 0)
        return "";
    return hier_name.substr(0, last_dot);
}

void ModuleTree::build(const Graph& graph) {
    modules_.clear();

    for (const auto& node : graph.nodes()) {
        if (node.module_path.empty())
            continue;

        std::string cur = node.module_path;
        std::unordered_set<std::string> visited;
        while (!cur.empty() && !visited.count(cur)) {
            visited.insert(cur);
            auto it = modules_.find(cur);
            if (it == modules_.end()) {
                ModuleNode mn;
                mn.name = cur;
                mn.parent_name = parent_module(cur);
                modules_[cur] = std::move(mn);
                it = modules_.find(cur);
            }
            if (node.kind == NodeKind::Register) {
                it->second.register_ids.push_back(node.id);
            }
            cur = it->second.parent_name;
        }

        std::string mod = node.module_path;
        std::string par = parent_module(mod);
        if (!par.empty()) {
            auto par_it = modules_.find(par);
            if (par_it == modules_.end()) {
                ModuleNode mn;
                mn.name = par;
                mn.parent_name = parent_module(par);
                modules_[par] = std::move(mn);
                par_it = modules_.find(par);
            }
            auto& children = par_it->second.children;
            if (std::find(children.begin(), children.end(), mod) == children.end()) {
                children.push_back(mod);
            }
        }
    }
}

const ModuleNode* ModuleTree::find(const std::string& module_name) const {
    auto it = modules_.find(module_name);
    return it != modules_.end() ? &it->second : nullptr;
}

std::vector<std::string> ModuleTree::ancestors(const std::string& module_name) const {
    std::vector<std::string> result;
    auto it = modules_.find(module_name);
    if (it == modules_.end())
        return result;
    std::string cur = it->second.parent_name;
    while (!cur.empty()) {
        result.push_back(cur);
        auto pit = modules_.find(cur);
        if (pit == modules_.end())
            break;
        cur = pit->second.parent_name;
    }
    return result;
}

std::string ModuleTree::common_ancestor(const std::string& a, const std::string& b) const {
    auto anc_a = ancestors(a);
    std::unordered_set<std::string> set_a(anc_a.begin(), anc_a.end());

    auto it = modules_.find(b);
    if (it == modules_.end())
        return "";
    std::string cur = it->second.parent_name;
    while (!cur.empty()) {
        if (set_a.count(cur))
            return cur;
        auto pit = modules_.find(cur);
        if (pit == modules_.end())
            break;
        cur = pit->second.parent_name;
    }
    return "";
}

size_t ModuleTree::depth(const std::string& module_name) const {
    return ancestors(module_name).size();
}

bool ModuleTree::crosses_boundary(uint64_t src_id, uint64_t dst_id, const Graph& graph) const {
    const Node* src = graph.find_node(src_id);
    const Node* dst = graph.find_node(dst_id);
    if (!src || !dst)
        return false;
    if (src->module_path.empty() || dst->module_path.empty())
        return false;
    return src->module_path != dst->module_path;
}

}  // namespace opencdc::ir
