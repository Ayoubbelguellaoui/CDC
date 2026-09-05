#include "analysis/analyzer.h"
#include "config/config.h"
#include "opencdc/opencdc.h"
#include "opencdc/version.h"
#include "report/html_reporter.h"
#include "report/report.h"
#ifdef OPENCDC_ENABLE_LSP
#include "lsp/server.h"
#endif
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace opencdc {

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <command> [options]\n"
        << "\nCommands:\n"
        << "  check <files...>     Analyse SystemVerilog files for CDC violations\n"
        << "  lsp                  Start the Language Server Protocol server\n"
        << "\ncheck options:\n"
        << "  --top <module>       Top module name (required)\n"
        << "  --config <file>      Configuration file (YAML)\n"
        << "  --waiver <file>      Waiver file\n"
        << "  --constraints <file> Clock constraints file (SDC or YAML)\n"
        << "  --format <fmt>       Output format: json, text, html (default: json)\n"
        << "  --out <file>         Write report to file (default: stdout)\n"
        << "  --html-dir <dir>     HTML report output directory (default: opencdc_report)\n"
        << "  --disable-rule <id>  Disable a rule (e.g., CDC001). Repeatable.\n"
        << "  --severity <id>=<sev> Override rule severity (e.g., CDC003=error). Repeatable.\n"
        << "  --false-path <s:d>   False path (e.g., top.src:top.dst). Repeatable.\n"
        << "  --verbose            Enable verbose output\n"
        << "\nlsp options:\n"
        << "  --top <module>       Top module name (required by analysis)\n"
        << "  --port <n>           TCP port to listen on (default: 2087)\n"
        << "  --config <file>      Configuration file (YAML)\n"
        << "  --waiver <file>      Waiver file\n"
        << "  --constraints <file> Clock constraints file\n"
        << "\nGeneral:\n"
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
        std::cerr << "OpenCDC v" << version_string() << "\n";
        return 0;
    }

    if (cmd != "check" && cmd != "lsp") {
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
        } else if (arg == "--constraints" && i + 1 < argc) {
            opts.constraints_path = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            opts.format = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if (arg == "--html-dir" && i + 1 < argc) {
            opts.html_output_dir = argv[++i];
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cerr << "OpenCDC v" << version_string() << "\n";
            return 0;
        } else if (arg == "--disable-rule" && i + 1 < argc) {
            opts.disable_rules.push_back(argv[++i]);
        } else if (arg == "--severity" && i + 1 < argc) {
            opts.severity_overrides.push_back(argv[++i]);
        } else if (arg == "--false-path" && i + 1 < argc) {
            std::string fp = argv[++i];
            auto colon = fp.find(':');
            if (colon != std::string::npos && colon > 0 && colon + 1 < fp.size() &&
                fp.find(':', colon + 1) == std::string::npos) {
                opts.false_paths.push_back({fp.substr(0, colon), fp.substr(colon + 1)});
            } else {
                std::cerr << "Error: --false-path requires non-empty src:dest format\n";
                return -1;
            }
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
    // Handle lsp subcommand before the shared parse_args path (which requires
    // input files — irrelevant for the LSP which receives content over the socket).
    if (argc >= 2 && std::string(argv[1]) == "lsp") {
#ifdef OPENCDC_ENABLE_LSP
        int port = 2087;
        std::string top_module;
        std::string config_path;
        std::string waiver_path;
        std::string constraints_path;

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                try {
                    port = std::stoi(argv[++i]);
                    if (port < 0 || port > 65535) {
                        std::cerr << "Error: --port must be between 0 and 65535\n";
                        return static_cast<int>(ExitCode::INPUT_ERROR);
                    }
                } catch (const std::exception&) {
                    std::cerr << "Error: --port requires a valid integer\n";
                    return static_cast<int>(ExitCode::INPUT_ERROR);
                }
            } else if (arg == "--top" && i + 1 < argc)
                top_module = argv[++i];
            else if (arg == "--config" && i + 1 < argc)
                config_path = argv[++i];
            else if (arg == "--waiver" && i + 1 < argc)
                waiver_path = argv[++i];
            else if (arg == "--constraints" && i + 1 < argc)
                constraints_path = argv[++i];
            else if (arg == "--help" || arg == "-h") {
                print_usage(argv[0]);
                return static_cast<int>(ExitCode::OK);
            }
        }

        lsp::LspServer server;
        server.set_top_module(top_module);
        server.set_config_path(config_path);
        server.set_waiver_path(waiver_path);
        server.set_constraints_path(constraints_path);
        server.start(port);
        std::cerr << "OpenCDC LSP server listening on port " << server.bound_port() << "\n";
        server.wait();
        return static_cast<int>(ExitCode::OK);
#else
        std::cerr << "Error: OpenCDC was built without LSP support.\n";
        return static_cast<int>(ExitCode::INPUT_ERROR);
#endif
    }

    CheckOptions opts;

    int result = parse_args(argc, argv, opts);
    if (result <= 0) {
        return result == 0 ? static_cast<int>(ExitCode::OK)
                           : static_cast<int>(ExitCode::INPUT_ERROR);
    }

    if (opts.format != "json" && opts.format != "text" && opts.format != "html") {
        std::cerr << "Error: unsupported output format: " << opts.format << "\n";
        return static_cast<int>(ExitCode::INPUT_ERROR);
    }

    // Config output section overrides CLI defaults (but not explicit CLI flags).
    std::string config_output_file;
    std::string config_output_format;
    config::Config parsed_cfg;
    bool have_parsed_config = false;
    if (!opts.config_path.empty()) {
        config::ConfigParser parser;
        std::string cfg_error;
        parsed_cfg = parser.parse_file(opts.config_path, &cfg_error);
        if (!cfg_error.empty()) {
            std::cerr << "Error: " << cfg_error << "\n";
            return static_cast<int>(ExitCode::INPUT_ERROR);
        }
        have_parsed_config = true;
        config_output_format = parsed_cfg.output.format;
        config_output_file = parsed_cfg.output.file;
    }
    if (opts.format == "json" && !config_output_format.empty()) {
        opts.format = config_output_format;
    }
    if (opts.output_path.empty() && opts.html_output_dir.empty() && !config_output_file.empty()) {
        if (opts.format == "html")
            opts.html_output_dir = config_output_file;
        else
            opts.output_path = config_output_file;
    }

    if (opts.verbose) {
        std::cerr << "OpenCDC v" << version_string() << "\n";
        std::cerr << "Top module: " << opts.top_module << "\n";
        std::cerr << "Input files:";
        for (const auto& f : opts.input_files)
            std::cerr << " " << f;
        std::cerr << "\n";
    }

    analysis::AnalysisRequest request;
    request.input_files = opts.input_files;
    request.top_module = opts.top_module;
    request.config_path = opts.config_path;
    request.waiver_path = opts.waiver_path;
    request.constraints_path = opts.constraints_path;
    request.false_paths = opts.false_paths;
    request.disable_rules = opts.disable_rules;
    request.severity_overrides = opts.severity_overrides;
    if (have_parsed_config) {
        request.config = std::move(parsed_cfg);
    }

    analysis::Analyzer analyzer;
    analysis::AnalysisResult analysis = analyzer.run(request);

    if (!analysis.ok) {
        for (const auto& err : analysis.errors) {
            std::cerr << "Error: " << err << "\n";
        }
        return static_cast<int>(ExitCode::INPUT_ERROR);
    }

    for (const auto& w : analysis.warnings) {
        std::cerr << "Warning: " << w << "\n";
    }

    if (opts.verbose) {
        std::cerr << "Graph: " << analysis.graph.register_count() << " registers, "
                  << analysis.graph.edge_count() << " edges\n";
        std::cerr << "Domains: " << analysis.domains.domains.size() << "\n";
        for (const auto& dom : analysis.domains.domains) {
            std::cerr << "  " << dom.name << ": " << dom.register_ids.size() << " register(s)\n";
        }
    }

    const auto& findings = analysis.findings;
    if (findings.empty() && opts.verbose) {
        std::cerr << "No CDC crossings detected.\n";
    }

    report::Reporter reporter;

    std::ofstream out_file;
    std::ostream* out = &std::cout;
    if (opts.format != "html" && !opts.output_path.empty()) {
        out_file.open(opts.output_path);
        if (!out_file.is_open()) {
            std::cerr << "Error: could not open output file: " << opts.output_path << "\n";
            return static_cast<int>(ExitCode::INPUT_ERROR);
        }
        out = &out_file;
    }

    if (opts.format == "text") {
        reporter.report_text(findings, *out, analysis.analysis_status);
        reporter.report_summary(findings, std::cerr, analysis.analysis_status);
    } else if (opts.format == "html") {
        report::HtmlReporter html_reporter;
        report::HtmlReportOptions html_opts;
        if (!opts.html_output_dir.empty()) {
            html_opts.output_dir = opts.html_output_dir;
        }
        try {
            html_reporter.generate_report(findings, html_opts, analysis.analysis_status);
        } catch (const std::exception& e) {
            std::cerr << "Error: could not generate HTML report: " << e.what() << "\n";
            return static_cast<int>(ExitCode::INPUT_ERROR);
        }
        if (opts.verbose) {
            std::cerr << "HTML report generated in " << html_opts.output_dir << "/\n";
        }
    } else {
        reporter.report_json(findings, *out, analysis.analysis_status);
    }

    return reporter.has_unsuppressed_errors(findings) ? static_cast<int>(ExitCode::FINDINGS)
                                                      : static_cast<int>(ExitCode::OK);
}

}  // namespace opencdc
