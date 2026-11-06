#include "report/domain_report.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace opencdc::report {

namespace {

using Registers = std::vector<const ir::Node*>;

std::string domain_of(const ir::Node& reg, const clock::DomainResult& domains) {
    auto it = domains.register_to_domain.find(reg.id);
    if (it == domains.register_to_domain.end() || it->second >= domains.domains.size())
        return "unresolved";
    return domains.domains[it->second].name;
}

std::string reset_kind(const ir::Node& reg) {
    std::string kind = reg.is_async_reset ? "async" : "sync";
    if (reg.reset_pol == ir::ResetPolarity::ActiveLow)
        kind += " active-low";
    else if (reg.reset_pol == ir::ResetPolarity::ActiveHigh)
        kind += " active-high";
    return kind;
}

size_t flip_flops(const Registers& regs) {
    size_t n = 0;
    for (const auto* r : regs)
        n += r->width;
    return n;
}

void sort_by_name(Registers& regs) {
    std::sort(regs.begin(), regs.end(),
              [](const ir::Node* a, const ir::Node* b) { return a->hier_name < b->hier_name; });
}

std::string location(const ir::Node& reg) {
    if (reg.loc.file.empty())
        return "-";
    return reg.loc.file + ":" + std::to_string(reg.loc.line);
}

// name  width  <column>  file:line
void print_registers(const Registers& regs, std::ostream& os,
                     std::string (*column)(const ir::Node&, const clock::DomainResult&),
                     const clock::DomainResult& domains) {
    size_t width = 0;
    for (const auto* r : regs)
        width = std::max(width, r->hier_name.size());
    for (const auto* r : regs) {
        os << "  " << r->hier_name << std::string(width - r->hier_name.size() + 2, ' ') << r->width
           << "  " << column(*r, domains) << "  " << location(*r) << "\n";
    }
}

std::string reset_column(const ir::Node& reg, const clock::DomainResult&) {
    if (reg.reset_signal.empty())
        return "no reset";
    return "reset " + reg.reset_signal + " (" + reset_kind(reg) + ")";
}

std::string clock_column(const ir::Node& reg, const clock::DomainResult& domains) {
    if (reg.reset_signal.empty())
        return "clock " + domain_of(reg, domains);
    return "clock " + domain_of(reg, domains) + " (" + reset_kind(reg) + ")";
}

std::string section(const std::string& title, const Registers& regs) {
    return title + ": " + std::to_string(regs.size()) + " register(s), " +
           std::to_string(flip_flops(regs)) + " flip-flop(s)";
}

Registers all_registers(const ir::Graph& graph) {
    Registers regs;
    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Register)
            regs.push_back(&node);
    }
    return regs;
}

}  // namespace

void DomainReporter::report_clocks(const ir::Graph& graph, const clock::DomainResult& domains,
                                   const std::string& top_module, std::ostream& os) const {
    std::map<std::string, Registers> by_clock;
    Registers unresolved;
    Registers regs = all_registers(graph);
    for (const auto* r : regs) {
        std::string name = domain_of(*r, domains);
        if (name == "unresolved")
            unresolved.push_back(r);
        else
            by_clock[name].push_back(r);
    }
    os << "# OpenCDC clock domains of " << top_module << ": " << by_clock.size() << " domain(s), "
       << regs.size() << " register(s), " << flip_flops(regs) << " flip-flop(s)\n"
       << "# register  width  reset  file:line\n";
    for (auto& [name, members] : by_clock) {
        sort_by_name(members);
        os << "\n" << section("clock " + name, members) << "\n";
        print_registers(members, os, reset_column, domains);
    }
    if (!unresolved.empty()) {
        sort_by_name(unresolved);
        os << "\n" << section("clock unresolved", unresolved) << "\n";
        print_registers(unresolved, os, reset_column, domains);
    }
}

void DomainReporter::report_resets(const ir::Graph& graph, const clock::DomainResult& domains,
                                   const std::string& top_module, std::ostream& os) const {
    std::map<std::string, Registers> by_reset;
    Registers no_reset;
    Registers regs = all_registers(graph);
    for (const auto* r : regs) {
        if (r->reset_signal.empty())
            no_reset.push_back(r);
        else
            by_reset[r->reset_signal].push_back(r);
    }
    os << "# OpenCDC reset signals of " << top_module << ": " << by_reset.size() << " reset(s), "
       << regs.size() << " register(s), " << flip_flops(regs) << " flip-flop(s)\n"
       << "# register  width  clock (reset kind)  file:line\n";
    for (auto& [name, members] : by_reset) {
        sort_by_name(members);
        std::set<std::string> clocks;
        for (const auto* r : members)
            clocks.insert(domain_of(*r, domains));
        std::string clock_list;
        for (const auto& c : clocks)
            clock_list += (clock_list.empty() ? "" : " ") + c;
        os << "\n"
           << section("reset " + name, members) << ", " << clocks.size()
           << " clock domain(s): " << clock_list << "\n";
        print_registers(members, os, clock_column, domains);
    }
    if (!no_reset.empty()) {
        sort_by_name(no_reset);
        os << "\n" << section("no reset", no_reset) << "\n";
        print_registers(no_reset, os, clock_column, domains);
    }
}

}  // namespace opencdc::report
