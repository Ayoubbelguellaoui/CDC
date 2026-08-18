#include "opencdc/opencdc.h"
#include "frontend/slang_adapter.h"
#include "clock/domain.h"
#include "cdc/crossing.h"
#include "cdc/reconvergence.h"
#include "cdc/waiver.h"
#include "rules/rule.h"
#include "report/report.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

namespace opencdc {

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " check <files...> [options]\n"
              << "\nOptions:\n"
              << "  --top <module>       Top module name (required)\n"
              << "  --config <file>      Configuration file (YAML)\n"
              << "  --waiver <file>      Waiver file (YAML)\n"
              << "  --format <fmt>       Output format: json, text (default: json)\n"
              << "  --out <file>         Write report to file (default: stdout)\n"
              << "  --disable-rule <id>  Disable a rule (e.g., CDC001). Repeatable.\n"
              << "  --severity <id>=<sev> Override rule severity (e.g., CDC003=error). Repeatable.\n"
              << "  --verbose            Enable verbose output\n"
              << "  --version            Show version information\n"
              << "  --help               Show this help message\n";
}

static int parse_args(int argc, const char* argv[], CheckOptions& opts) {
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }

    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_usage(argv[0]);
        return 0;
    }

    if (cmd == "--version" || cmd == "-v") {
        std::cerr << "OpenCDC v0.1.0\n";
        return 0;
    }

    if (cmd != "check") {
        std::cerr << "Error: unknown command '" << cmd << "'\n";
        print_usage(argv[0]);
        return -1;
    }

    int i = 2;
    bool files_found = false;
    while (i < argc) {
        std::string arg = argv[i];

        if (arg == "--top" && i + 1 < argc) {
            opts.top_module = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            opts.config_path = argv[++i];
        } else if (arg == "--waiver" && i + 1 < argc) {
            opts.waiver_path = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            opts.format = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cerr << "OpenCDC v0.1.0\n";
            return 0;
        } else if (arg == "--disable-rule" && i + 1 < argc) {
            opts.disable_rules.push_back(argv[++i]);
        } else if (arg == "--severity" && i + 1 < argc) {
            opts.severity_overrides.push_back(argv[++i]);
        } else if (!arg.empty() && arg[0] != '-') {
            opts.input_files.push_back(arg);
            files_found = true;
        } else {
            std::cerr << "Error: unknown option '" << arg << "'\n";
            return -1;
        }
        i++;
    }

    if (!files_found) {
        std::cerr << "Error: no input files specified\n";
        return -1;
    }

    if (opts.top_module.empty()) {
        std::cerr << "Error: --top <module> is required\n";
        return -1;
    }

    return 1;
}

int run(int argc, const char* argv[]) {
    CheckOptions opts;

    int result = parse_args(argc, argv, opts);
    if (result <= 0) {
        return result == 0 ? static_cast<int>(ExitCode::OK)
                           : static_cast<int>(ExitCode::INPUT_ERROR);
    }

    if (opts.verbose) {
        std::cerr << "OpenCDC v0.1.0\n";
        std::cerr << "Top module: " << opts.top_module << "\n";
        std::cerr << "Input files:";
        for (const auto& f : opts.input_files) std::cerr << " " << f;
        std::cerr << "\n";
    }

    frontend::SlangAdapter adapter;
    frontend::FrontendResult fe_result = adapter.elaborate(
        opts.input_files, opts.top_module);

    if (!fe_result.ok) {
        for (const auto& err : fe_result.errors) {
            std::cerr << "Error: " << err << "\n";
        }
        return static_cast<int>(ExitCode::INTERNAL_ERROR);
    }

    clock::DomainExtractor domain_extractor;
    clock::DomainResult dom_result = domain_extractor.extract(fe_result.graph);

    for (const auto& w : dom_result.warnings) {
        std::cerr << "Warning: " << w << "\n";
    }

    if (opts.verbose) {
        std::cerr << "Graph: " << fe_result.graph.register_count()
                  << " registers, " << fe_result.graph.edge_count()
                  << " edges\n";
        std::cerr << "Domains: " << dom_result.domains.size() << "\n";
        for (const auto& dom : dom_result.domains) {
            std::cerr << "  " << dom.name << ": "
                      << dom.register_ids.size() << " register(s)\n";
        }
    }

    cdc::CrossingAnalyzer crossing_analyzer;
    auto findings = crossing_analyzer.analyze(fe_result.graph, dom_result.domains);

    cdc::ReconvergenceAnalyzer reconvergence_analyzer;
    auto reconv_findings = reconvergence_analyzer.analyze(
        fe_result.graph, dom_result.domains, findings);

    for (auto& f : reconv_findings) {
        findings.push_back(std::move(f));
    }

    rules::RuleEngine rule_engine;
    for (const auto& id : opts.disable_rules) {
        rule_engine.add_override({id, "", true, false});
    }
    for (const auto& s : opts.severity_overrides) {
        auto eq = s.find('=');
        if (eq != std::string::npos) {
            rule_engine.add_override({s.substr(0, eq), s.substr(eq + 1), false, true});
        }
    }
    findings = rule_engine.filter(findings);

    cdc::WaiverEngine waiver_engine;
    if (!opts.waiver_path.empty()) {
        if (!waiver_engine.load_from_file(opts.waiver_path)) {
            std::cerr << "Error: could not load waiver file: " << opts.waiver_path << "\n";
            return static_cast<int>(ExitCode::INPUT_ERROR);
        }
        findings = waiver_engine.apply(findings);
    }

    report::Reporter reporter;

    if (findings.empty()) {
        if (opts.verbose) {
            std::cerr << "No CDC crossings detected.\n";
        }
        return static_cast<int>(ExitCode::OK);
    }

    std::ofstream out_file;
    std::ostream* out = &std::cout;
    if (!opts.output_path.empty()) {
        out_file.open(opts.output_path);
        if (!out_file.is_open()) {
            std::cerr << "Error: could not open output file: " << opts.output_path << "\n";
            return static_cast<int>(ExitCode::INPUT_ERROR);
        }
        out = &out_file;
    }

    if (opts.format == "json") {
        reporter.report_json(findings, *out);
    } else {
        reporter.report_text(findings, *out);
        reporter.report_summary(findings, std::cerr);
    }

    return reporter.has_unsuppressed_errors(findings)
               ? static_cast<int>(ExitCode::FINDINGS)
               : static_cast<int>(ExitCode::OK);
}

} // namespace opencdc
