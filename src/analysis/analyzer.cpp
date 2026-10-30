#include "analysis/analyzer.h"

#include <memory>
#include <unordered_set>

#include "cdc/blackbox.h"
#include "cdc/cdc006.h"
#include "cdc/pattern.h"
#include "cdc/reconvergence.h"
#include "cdc/reset_domain.h"
#include "cdc/waiver.h"
#include "clock/constraints.h"
#include "clock/resolve.h"
#include "config/config.h"
#include "config/profile.h"
#include "frontend/slang_adapter.h"
#include "ir/module_tree.h"
#include "rules/rule.h"

namespace opencdc::analysis {

static void apply_request_profile(const AnalysisRequest& request, config::Config& cfg) {
    if (request.profile.empty())
        return;
    config::ProfileSettings settings = config::get_profile_settings(request.profile);
    config::apply_profile(settings, cfg);
}

AnalysisResult Analyzer::run(const AnalysisRequest& request) {
    // Parse config FIRST: FrontendOptions depend on it (allow_user_annotation),
    // and config errors are cheaper to report than elaboration errors.
    config::Config cfg;
    if (request.config.has_value()) {
        cfg = *request.config;
    } else if (!request.config_path.empty()) {
        config::ConfigParser parser;
        std::string cfg_error;
        cfg = parser.parse_file(request.config_path, &cfg_error);
        if (!cfg_error.empty()) {
            AnalysisResult result;
            result.errors.push_back(std::move(cfg_error));
            result.analysis_status = "failed";
            return result;
        }
    }
    apply_request_profile(request, cfg);

    // 1. Frontend: parse and elaborate to IR graph. InputType::Auto runs the
    // conservative per-file heuristic (ambiguous → rtl, preserving the
    // historical default).
    frontend::InputType input_type = request.input_type;
    if (input_type == frontend::InputType::Auto)
        input_type = frontend::detect_input_type(request.input_files);

    std::unique_ptr<frontend::IFrontend> adapter;
    if (input_type == frontend::InputType::Netlist) {
        adapter = frontend::make_netlist_adapter();
        if (!adapter) {
            AnalysisResult result;
            result.errors.push_back(
                "netlist input requested but the netlist frontend is not available in this build");
            result.analysis_status = "failed";
            return result;
        }
    } else {
        adapter = std::make_unique<frontend::SlangAdapter>();
    }

    frontend::FrontendOptions fe_opts;
    fe_opts.include_dirs = request.include_dirs;
    fe_opts.defines = request.defines;
    fe_opts.allow_user_annotation = cfg.allow_user_annotation;
    fe_opts.muxed_clock_domain = cfg.muxed_clock_domain;
    fe_opts.cell_map_path =
        !request.cell_map_path.empty() ? request.cell_map_path : cfg.cell_map_path;
    fe_opts.liberty_path = !request.liberty_path.empty() ? request.liberty_path : cfg.liberty_path;
    fe_opts.name_map_path =
        !request.name_map_path.empty() ? request.name_map_path : cfg.name_map_path;
    fe_opts.max_nodes = request.max_nodes ? request.max_nodes : cfg.max_nodes;
    fe_opts.max_edges = request.max_edges ? request.max_edges : cfg.max_edges;
    frontend::FrontendResult fe_result =
        adapter->elaborate(request.input_files, request.top_module, fe_opts);
    if (!fe_result.ok) {
        AnalysisResult result;
        result.errors = std::move(fe_result.errors);
        result.warnings = std::move(fe_result.warnings);
        result.input_type = fe_result.input_type;
        result.analysis_status = "failed";
        return result;
    }
    ir::Graph graph = std::move(fe_result.graph);
    std::vector<std::string> fe_warnings = std::move(fe_result.warnings);
    bool incomplete_frontend = fe_result.suspected_sequential_unknown;

    AnalysisResult result = run_on_graph(std::move(graph), request, std::move(cfg));
    // Frontend warnings (e.g. skipped unknown-clock blocks) travel with the result.
    result.warnings.insert(result.warnings.begin(), fe_warnings.begin(), fe_warnings.end());
    result.input_type = fe_result.input_type;

    // Unmapped sequential-like cells were modeled as opaque combinational:
    // their flops and crossings are missing, so the analysis cannot claim to
    // be complete. Downgrade status (and re-evaluate coverage/signoff) so
    // --signoff reports INCOMPLETE instead of a false "complete"/PASS.
    if (incomplete_frontend && result.analysis_status == "complete") {
        result.analysis_status = "incomplete";
        CoverageEngine coverage_engine;
        result.coverage = coverage_engine.compute(result.findings, result.analysis_status);
        coverage_engine.compute_crossing_coverage(result.coverage, result.graph,
                                                  result.domains.domains,
                                                  result.domains.register_to_domain);
        SignoffEngine signoff_engine;
        result.signoff =
            signoff_engine.evaluate(result.findings, result.analysis_status, config::Config{},
                                    request.profile.empty() ? "default" : request.profile);
    }
    return result;
}

AnalysisResult Analyzer::run_on_graph(ir::Graph graph, const AnalysisRequest& request,
                                      config::Config cfg) {
    AnalysisResult result;
    result.analysis_status = "complete";
    result.graph = std::move(graph);

    // 2. Constraints file (SDC or YAML), if any.
    clock::ClockConstraints constraints;
    bool have_constraints_file = !request.constraints_path.empty();
    if (have_constraints_file) {
        clock::ConstraintsParser constraints_parser;
        std::string constraints_error;
        constraints = constraints_parser.parse_file(request.constraints_path, &constraints_error);
        if (!constraints_error.empty()) {
            result.errors.push_back(std::move(constraints_error));
            result.analysis_status = "failed";
            return result;
        }

        // Generated clocks: remap registers to their master clock root.
        for (auto& node : result.graph.nodes_mutable()) {
            if (node.kind != ir::NodeKind::Register)
                continue;
            auto clock_def = constraints.get_clock(node.clock_domain);
            if (!clock_def || !clock_def->is_generated || clock_def->master_clock.empty())
                continue;
            node.root_clock = clock_def->master_clock;
        }
    }

    // 3. Clock domain extraction.
    clock::DomainExtractor domain_extractor;
    result.domains = domain_extractor.extract(result.graph);
    result.warnings = result.domains.warnings;

    // 4. Config: rule overrides, waivers, false paths, reset policy.
    // (Parsed by run() before elaboration; passed in so FrontendOptions can
    // depend on it.)
    rules::RuleEngine rule_engine;

    for (const auto& [rule_id, rule_cfg] : cfg.rules) {
        if (!rule_engine.find_rule(rule_id).has_value()) {
            result.errors.push_back("unknown rule in config: " + rule_id);
            result.analysis_status = "failed";
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
    std::unordered_set<std::string> fp_keys;
    for (const auto& fp : constraints.false_paths) {
        fp_keys.insert(fp.from_clock + std::string("\0", 1) + fp.to_clock);
    }
    auto add_exclusive_fps = [&](const std::vector<std::string>& clocks) {
        for (size_t i = 0; i < clocks.size(); ++i) {
            for (size_t j = i + 1; j < clocks.size(); ++j) {
                auto add_if_new = [&](const std::string& a, const std::string& b) {
                    std::string key = a + std::string("\0", 1) + b;
                    if (!fp_keys.insert(key).second)
                        return;
                    clock::FalsePath fp;
                    fp.from_clock = a;
                    fp.to_clock = b;
                    constraints.false_paths.push_back(fp);
                };
                add_if_new(clocks[i], clocks[j]);
                add_if_new(clocks[j], clocks[i]);
            }
        }
    };

    for (const auto& grp : cfg.clock_groups) {
        if (grp.exclusive)
            add_exclusive_fps(grp.clocks);
    }

    // 5. Request-level false paths (CLI --false-path or programmatic).
    for (const auto& fp : request.false_paths) {
        clock::FalsePath constraint_fp;
        constraint_fp.from_reg = fp.first;
        constraint_fp.to_reg = fp.second;
        constraints.false_paths.push_back(constraint_fp);
    }

    // 6. Request-level rule overrides.
    for (const auto& id : request.disable_rules) {
        if (!rule_engine.find_rule(id).has_value()) {
            result.errors.push_back("unknown rule: " + id);
            result.analysis_status = "failed";
            return result;
        }
        rule_engine.add_override({id, "", true, false});
    }
    for (const auto& s : request.severity_overrides) {
        auto eq = s.find('=');
        if (eq == std::string::npos) {
            result.errors.push_back("invalid severity override '" + s +
                                    "': expected RULE=SEVERITY");
            result.analysis_status = "failed";
            return result;
        }
        std::string rule_id = s.substr(0, eq);
        std::string severity = s.substr(eq + 1);
        if (!rule_engine.find_rule(rule_id).has_value() ||
            (severity != "error" && severity != "warning" && severity != "info")) {
            result.errors.push_back("invalid severity override: " + s);
            result.analysis_status = "failed";
            return result;
        }
        rule_engine.add_override({rule_id, severity, false, true});
    }

    // 7. Exclusive clock groups from SDC file (deduped via add_exclusive_fps).
    if (have_constraints_file) {
        for (const auto& group : constraints.clock_groups) {
            if (group.exclusive)
                add_exclusive_fps(group.clocks);
        }
    }

    // 8. Pattern recognition, then crossing analysis.
    cdc::PatternRecognizer pattern_recognizer;
    pattern_recognizer.set_require_structural_proof(cfg.require_structural_proof);
    pattern_recognizer.analyze_and_annotate(result.graph);

    // 8b. Blackbox registry: built-in models + user-configured.
    cdc::BlackBoxRegistry blackbox_registry;
    for (const auto& bb : cfg.blackboxes) {
        cdc::BlackBoxModel model;
        model.module_name = bb.module_name;
        model.vendor = bb.vendor;
        model.properties.is_safe_crossing = bb.is_safe_crossing;
        model.properties.has_synchronizer = bb.has_synchronizer;
        model.properties.has_gray_encoding = bb.has_gray_encoding;
        model.properties.has_async_fifo = bb.has_async_fifo;
        model.properties.has_handshake = bb.has_handshake;
        blackbox_registry.add_model(model);
    }

    // 8c. Module tree for boundary analysis.
    ir::ModuleTree module_tree;
    module_tree.build(result.graph);

    // Clock resolve for relationship classification.
    clock::ClockResolver resolver;
    resolver.set_muxed_clock_domain(cfg.muxed_clock_domain);
    clock::ResolveResult resolve_result = resolver.resolve(result.graph);

    cdc::CrossingAnalyzer crossing_analyzer;
    crossing_analyzer.set_pattern_recognizer(&pattern_recognizer);
    // Always attach constraints: config and request false paths must be
    // honored even when no constraints file was provided.
    crossing_analyzer.set_clock_constraints(&constraints);
    crossing_analyzer.set_multicycle_policy(&cfg.multicycle_path_policy);
    crossing_analyzer.set_reset_policy(&cfg.reset_policy);
    crossing_analyzer.set_resolve_result(&resolve_result);
    crossing_analyzer.set_blackbox_registry(&blackbox_registry);
    crossing_analyzer.set_min_sync_stages(cfg.min_sync_stages);
    crossing_analyzer.set_module_tree(&module_tree);
    auto findings =
        crossing_analyzer.analyze(result.graph, result.domains.domains,
                                  result.domains.register_to_domain, request.num_threads);

    // Propagate value uncertainty downstream after crossing detection.
    crossing_analyzer.propagate_uncertainty(result.graph, findings);

    // 9. Reconvergence.
    cdc::ReconvergenceAnalyzer reconvergence_analyzer;
    reconvergence_analyzer.set_max_hops(cfg.reconvergence_depth);
    reconvergence_analyzer.set_report_double_synchronization(cfg.report_double_synchronization);
    auto reconv_findings =
        reconvergence_analyzer.analyze(result.graph, result.domains.domains, findings);
    for (auto& f : reconv_findings) {
        findings.push_back(std::move(f));
    }

    // 10. CDC006: combinational logic between synchronizer stages.
    cdc::Cdc006Analyzer cdc006_analyzer;
    auto cdc006_findings = cdc006_analyzer.analyze(result.graph, result.domains.domains,
                                                   result.domains.register_to_domain);
    for (auto& f : cdc006_findings) {
        findings.push_back(std::move(f));
    }

    // 11. Reset domain crossings.
    cdc::ResetDomainAnalyzer reset_domain_analyzer;
    auto reset_findings = reset_domain_analyzer.check_reset_crossings(
        result.graph, {}, result.domains.domains, result.domains.register_to_domain,
        &cfg.reset_policy);
    // synchronized_crossings_safe: no CDC009 on a pair whose crossing is
    // verified (a synchronizer or a handshake payload): the source's reset is
    // one more asynchronous change, resolved as the crossing's data is.
    std::unordered_set<uint64_t> verified_pairs;
    if (cfg.reset_policy.synchronized_crossings_safe) {
        for (const auto& f : findings) {
            if (f.rule_id == "CDC001" && f.safety_status == cdc::SafetyStatus::VerifiedSafe &&
                f.source_reg_id <= UINT32_MAX && f.dest_reg_id <= UINT32_MAX)
                verified_pairs.insert((f.source_reg_id << 32) | f.dest_reg_id);
        }
    }
    for (auto& f : reset_findings) {
        if (cfg.suppress_reset_crossings && f.rule_id == "CDC009")
            continue;
        if (f.rule_id == "CDC009" && verified_pairs.count((f.source_reg_id << 32) | f.dest_reg_id))
            continue;
        findings.push_back(std::move(f));
    }

    // 11b. Low-power isolation cells: CDC001 whose path crosses a node
    // flagged by the netlist frontend as an isolation cell is downgraded to
    // info/VerifiedSafe with provenance — the cell is a methodology-managed
    // power-domain boundary (UPF is out of scope), NOT a synchronizer. Only
    // CDC001 is affected: CDC002/004/005/007 are independent of synchronizer
    // presence and stay fully evaluated on the same crossing. No input-type
    // gate is needed here: is_isolation_cell is set exclusively by the
    // netlist frontend (regroup preserves it), so RTL graphs never match and
    // RTL runs are structurally unaffected. Note: result.input_type is only
    // assigned by run() after run_on_graph returns, so it cannot gate this.
    for (auto& f : findings) {
        if (f.rule_id != "CDC001" || f.severity != "error")
            continue;
        for (uint64_t nid : f.path.node_ids) {
            const ir::Node* n = result.graph.find_node(nid);
            if (n && n->is_isolation_cell) {
                f.severity = "info";
                f.safety_status = cdc::SafetyStatus::VerifiedSafe;
                f.safety_provenance = "Path through low-power isolation cell";
                f.reason =
                    "CDC001 downgraded: path passes through a low-power isolation cell "
                    "(methodology-managed power-domain boundary, not a synchronizer; "
                    "CDC002/004/005/007 remain evaluated).";
                break;
            }
        }
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
            result.warnings.push_back("Waiver for rule '" + w.rule_id +
                                      "' ignored: invalid wildcard/regex pattern");
        }
    }
    if (!request.waiver_path.empty()) {
        std::string waiver_error;
        if (!waiver_engine.load_from_file(request.waiver_path, &waiver_error)) {
            result.errors.push_back("could not load waiver file: " + request.waiver_path +
                                    (waiver_error.empty() ? "" : " (" + waiver_error + ")"));
            result.analysis_status = "failed";
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

    for (const auto& f : result.findings) {
        if (f.rule_id == "CDC010") {
            result.analysis_status = "incomplete";
            break;
        }
    }

    // 14. Compute coverage and signoff.
    CoverageEngine coverage_engine;
    result.coverage = coverage_engine.compute(result.findings, result.analysis_status);
    coverage_engine.compute_crossing_coverage(result.coverage, result.graph, result.domains.domains,
                                              result.domains.register_to_domain);

    SignoffEngine signoff_engine;
    result.signoff = signoff_engine.evaluate(result.findings, result.analysis_status, cfg,
                                             request.profile.empty() ? "default" : request.profile);

    return result;
}

AnalysisResult Analyzer::run_incremental(AnalysisResult& previous, const AnalysisRequest& request) {
    if (!previous.graph.dirty()) {
        AnalysisResult keep = previous;
        keep.ok = true;
        keep.analysis_status = "no_changes";
        return keep;
    }

    // Re-run the analysis stages on the caller's mutated graph (no
    // re-elaboration from files). Parse config the same way run() does so
    // FrontendOptions-dependent settings stay consistent.
    config::Config cfg;
    if (request.config.has_value()) {
        cfg = *request.config;
    } else if (!request.config_path.empty()) {
        config::ConfigParser parser;
        std::string cfg_error;
        cfg = parser.parse_file(request.config_path, &cfg_error);
        if (!cfg_error.empty()) {
            AnalysisResult result;
            result.errors.push_back(std::move(cfg_error));
            result.analysis_status = "failed";
            return result;
        }
    }
    apply_request_profile(request, cfg);
    AnalysisResult result = run_on_graph(previous.graph, request, std::move(cfg));
    if (result.ok)
        previous = result;
    return result;
}

}  // namespace opencdc::analysis
