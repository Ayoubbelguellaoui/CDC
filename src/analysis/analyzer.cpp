#include "analysis/analyzer.h"
#include "frontend/slang_adapter.h"
#include "clock/constraints.h"
#include "cdc/reconvergence.h"
#include "cdc/cdc006.h"
#include "cdc/reset_domain.h"
#include "cdc/waiver.h"
#include "cdc/pattern.h"
#include "rules/rule.h"
#include "config/config.h"

namespace opencdc::analysis {

AnalysisResult Analyzer::run(const AnalysisRequest& request) {
    AnalysisResult result;

    // 1. Frontend: parse and elaborate to IR graph.
    frontend::SlangAdapter adapter;
    frontend::FrontendResult fe_result =
        adapter.elaborate(request.input_files, request.top_module);
    if (!fe_result.ok) {
        result.errors = std::move(fe_result.errors);
        return result;
    }
    result.graph = std::move(fe_result.graph);

    // 2. Constraints file (SDC or YAML), if any.
    clock::ClockConstraints constraints;
    bool have_constraints_file = !request.constraints_path.empty();
    if (have_constraints_file) {
        clock::ConstraintsParser constraints_parser;
        std::string constraints_error;
        constraints = constraints_parser.parse_file(request.constraints_path,
                                                    &constraints_error);
        if (!constraints_error.empty()) {
            result.errors.push_back(std::move(constraints_error));
            return result;
        }

        // Generated clocks: remap registers to their master clock root.
        for (auto& node : result.graph.nodes_mutable()) {
            if (node.kind != ir::NodeKind::Register) continue;
            auto clock_def = constraints.get_clock(node.clock_domain);
            if (!clock_def || !clock_def->is_generated ||
                clock_def->master_clock.empty()) continue;
            node.root_clock = clock_def->master_clock;
        }
    }

    // 3. Clock domain extraction.
    clock::DomainExtractor domain_extractor;
    result.domains = domain_extractor.extract(result.graph);
    result.warnings = result.domains.warnings;

    // 4. Config file: rule overrides, waivers, false paths, reset policy.
    rules::RuleEngine rule_engine;
    config::Config cfg;
    if (request.config.has_value()) {
        cfg = *request.config;
    } else if (!request.config_path.empty()) {
        config::ConfigParser parser;
        std::string cfg_error;
        cfg = parser.parse_file(request.config_path, &cfg_error);
        if (!cfg_error.empty()) {
            result.errors.push_back(std::move(cfg_error));
            return result;
        }
    }

    for (const auto& [rule_id, rule_cfg] : cfg.rules) {
        if (!rule_engine.find_rule(rule_id).has_value()) {
            result.errors.push_back("unknown rule in config: " + rule_id);
            return result;
        }
        if (!rule_cfg.enabled) {
            rule_engine.add_override({rule_id, "", true, false});
        }
        if (!rule_cfg.severity.empty()) {
            rule_engine.add_override({rule_id, rule_cfg.severity, false, true});
        }
    }

    for (const auto& fp : cfg.false_paths) {
        clock::FalsePath constraint_fp;
        constraint_fp.from_reg = fp.source_reg;
        constraint_fp.to_reg = fp.dest_reg;
        constraint_fp.from_clock = fp.source_clock;
        constraint_fp.to_clock = fp.dest_clock;
        constraints.false_paths.push_back(constraint_fp);
    }

    // Helper: expand an exclusive clock group into bidirectional false paths,
    // deduplicating against already-added paths to avoid doubles when both the
    // config YAML and the SDC file define the same group.
    auto add_exclusive_fps = [&](const std::vector<std::string>& clocks) {
        for (size_t i = 0; i < clocks.size(); ++i) {
            for (size_t j = i + 1; j < clocks.size(); ++j) {
                auto add_if_new = [&](const std::string& a, const std::string& b) {
                    for (const auto& fp : constraints.false_paths) {
                        if (fp.from_clock == a && fp.to_clock == b) return;
                    }
                    clock::FalsePath fp;
                    fp.from_clock = a;
                    fp.to_clock   = b;
                    constraints.false_paths.push_back(fp);
                };
                add_if_new(clocks[i], clocks[j]);
                add_if_new(clocks[j], clocks[i]);
            }
        }
    };

    for (const auto& grp : cfg.clock_groups) {
        if (grp.exclusive) add_exclusive_fps(grp.clocks);
    }

    // 5. Request-level false paths (CLI --false-path or programmatic).
    for (const auto& fp : request.false_paths) {
        clock::FalsePath constraint_fp;
        constraint_fp.from_reg = fp.first;
        constraint_fp.to_reg   = fp.second;
        constraints.false_paths.push_back(constraint_fp);
    }

    // 6. Request-level rule overrides.
    for (const auto& id : request.disable_rules) {
        if (!rule_engine.find_rule(id).has_value()) {
            result.errors.push_back("unknown rule: " + id);
            return result;
        }
        rule_engine.add_override({id, "", true, false});
    }
    for (const auto& s : request.severity_overrides) {
        auto eq = s.find('=');
        if (eq == std::string::npos) {
            result.errors.push_back("invalid severity override '" + s +
                                    "': expected RULE=SEVERITY");
            return result;
        }
        std::string rule_id = s.substr(0, eq);
        std::string severity = s.substr(eq + 1);
        if (!rule_engine.find_rule(rule_id).has_value() ||
            (severity != "error" && severity != "warning" && severity != "info")) {
            result.errors.push_back("invalid severity override: " + s);
            return result;
        }
        rule_engine.add_override({rule_id, severity, false, true});
    }

    // 7. Exclusive clock groups from SDC file (deduped via add_exclusive_fps).
    if (have_constraints_file) {
        for (const auto& group : constraints.clock_groups) {
            if (group.exclusive) add_exclusive_fps(group.clocks);
        }
    }

    // 8. Pattern recognition, then crossing analysis.
    cdc::PatternRecognizer pattern_recognizer;
    pattern_recognizer.analyze_and_annotate(result.graph);

    cdc::CrossingAnalyzer crossing_analyzer;
    crossing_analyzer.set_pattern_recognizer(&pattern_recognizer);
    // Always attach constraints: config and request false paths must be
    // honored even when no constraints file was provided.
    crossing_analyzer.set_clock_constraints(&constraints);
    auto findings = crossing_analyzer.analyze(result.graph,
                                              result.domains.domains,
                                              result.domains.register_to_domain);

    // 9. Reconvergence.
    cdc::ReconvergenceAnalyzer reconvergence_analyzer;
    auto reconv_findings = reconvergence_analyzer.analyze(
        result.graph, result.domains.domains, findings);
    for (auto& f : reconv_findings) {
        findings.push_back(std::move(f));
    }

    // 10. CDC006: combinational logic between synchronizer stages.
    cdc::Cdc006Analyzer cdc006_analyzer;
    auto cdc006_findings = cdc006_analyzer.analyze(
        result.graph, result.domains.domains, result.domains.register_to_domain);
    for (auto& f : cdc006_findings) {
        findings.push_back(std::move(f));
    }

    // 11. Reset domain crossings.
    cdc::ResetDomainAnalyzer reset_domain_analyzer;
    auto reset_findings = reset_domain_analyzer.check_reset_crossings(
        result.graph, {}, result.domains.domains,
        result.domains.register_to_domain);
    for (auto& f : reset_findings) {
        if (cfg.suppress_reset_crossings && f.rule_id == "CDC009") continue;
        findings.push_back(std::move(f));
    }

    // 12. Rule enable/severity filtering.
    findings = rule_engine.filter(findings);

    // 13. Waivers from config and waiver file.
    cdc::WaiverEngine waiver_engine;
    for (const auto& w : cfg.waivers) {
        cdc::Waiver waiver;
        waiver.rule_id = w.rule_id;
        waiver.source_reg_name = w.source_reg;
        waiver.dest_reg_name = w.dest_reg;
        waiver.source_domain = w.source_domain;
        waiver.dest_domain = w.dest_domain;
        waiver.justification = w.justification;
        waiver.owner = w.owner;
        waiver.expiry = w.expiry;
        if (!waiver_engine.add_waiver(waiver)) {
            result.warnings.push_back(
                "Waiver for rule '" + w.rule_id +
                "' ignored: invalid wildcard/regex pattern");
        }
    }
    if (!request.waiver_path.empty()) {
        std::string waiver_error;
        if (!waiver_engine.load_from_file(request.waiver_path, &waiver_error)) {
            result.errors.push_back("could not load waiver file: " +
                                    request.waiver_path +
                                    (waiver_error.empty() ? "" : " (" + waiver_error + ")"));
            return result;
        }
    }
    if (!waiver_engine.waivers().empty()) {
        findings = waiver_engine.apply(findings);

        auto waiver_warnings = waiver_engine.check_unused(findings);
        for (auto& w : waiver_warnings) {
            result.warnings.push_back(std::move(w));
        }
    }

    result.findings = std::move(findings);
    result.ok = true;
    return result;
}

} // namespace opencdc::analysis
