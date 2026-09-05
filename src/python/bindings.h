#ifndef OPENCDC_PYTHON_BINDINGS_H
#define OPENCDC_PYTHON_BINDINGS_H

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "analysis/trend.h"
#include "cdc/crossing.h"
#include "cdc/pattern.h"
#include "clock/constraints.h"
#include "clock/domain.h"
#include "ir/graph.h"
#include "opencdc/opencdc.h"
#include "report/html_reporter.h"
#include "report/report.h"

namespace py = pybind11;

namespace opencdc {
namespace python {

void init_graph_bindings(py::module& m) {
    py::class_<ir::SourceLoc>(m, "SourceLoc")
        .def(py::init<>())
        .def_readwrite("file", &ir::SourceLoc::file)
        .def_readwrite("line", &ir::SourceLoc::line)
        .def_readwrite("col", &ir::SourceLoc::col);

    py::enum_<ir::NodeKind>(m, "NodeKind")
        .value("Register", ir::NodeKind::Register)
        .value("Port", ir::NodeKind::Port)
        .value("Net", ir::NodeKind::Net)
        .value("Combinational", ir::NodeKind::Combinational);

    py::enum_<ir::ResetPolarity>(m, "ResetPolarity")
        .value("ActiveHigh", ir::ResetPolarity::ActiveHigh)
        .value("ActiveLow", ir::ResetPolarity::ActiveLow)
        .value("None", ir::ResetPolarity::None);

    py::enum_<ir::LogicType>(m, "LogicType")
        .value("None", ir::LogicType::None)
        .value("And", ir::LogicType::And)
        .value("Or", ir::LogicType::Or)
        .value("Xor", ir::LogicType::Xor)
        .value("Not", ir::LogicType::Not)
        .value("Mux", ir::LogicType::Mux)
        .value("Concat", ir::LogicType::Concat)
        .value("GrayEncoder", ir::LogicType::GrayEncoder)
        .value("GrayDecoder", ir::LogicType::GrayDecoder)
        .value("HandshakeValid", ir::LogicType::HandshakeValid)
        .value("HandshakeReady", ir::LogicType::HandshakeReady)
        .value("AsyncFifoPtr", ir::LogicType::AsyncFifoPtr)
        .value("Unknown", ir::LogicType::Unknown);

    py::class_<ir::Node>(m, "Node")
        .def_readonly("id", &ir::Node::id)
        .def_readwrite("hier_name", &ir::Node::hier_name)
        .def_readwrite("short_name", &ir::Node::short_name)
        .def_readonly("kind", &ir::Node::kind)
        .def_readwrite("width", &ir::Node::width)
        .def_readwrite("clock_domain", &ir::Node::clock_domain)
        .def_readwrite("root_clock", &ir::Node::root_clock)
        .def_readwrite("clock_is_gated", &ir::Node::clock_is_gated)
        .def_readwrite("clock_is_muxed", &ir::Node::clock_is_muxed)
        .def_readwrite("reset_signal", &ir::Node::reset_signal)
        .def_readwrite("reset_pol", &ir::Node::reset_pol)
        .def_readonly("loc", &ir::Node::loc)
        .def_readwrite("logic_type", &ir::Node::logic_type)
        .def_readwrite("is_gray_coded", &ir::Node::is_gray_coded)
        .def_readwrite("is_handshake_signal", &ir::Node::is_handshake_signal)
        .def_readwrite("is_async_fifo_ptr", &ir::Node::is_async_fifo_ptr);

    py::class_<ir::Graph>(m, "Graph")
        .def(py::init<>())
        .def("add_register", &ir::Graph::add_register, py::arg("hier_name"),
             py::arg("clock_domain"), py::arg("width") = 1, py::arg("loc") = ir::SourceLoc{})
        .def("add_port", &ir::Graph::add_port, py::arg("hier_name"), py::arg("width") = 1,
             py::arg("loc") = ir::SourceLoc{})
        .def("add_edge", &ir::Graph::add_edge, py::arg("from_id"), py::arg("to_id"))
        .def(
            "find_node",
            [](const ir::Graph& g, uint64_t id) {
                auto* node = g.find_node(id);
                return node ? py::cast(*node) : py::none();
            },
            py::arg("id"))
        .def(
            "find_node_by_name",
            [](const ir::Graph& g, const std::string& name) {
                auto* node = g.find_node_by_name(name);
                return node ? py::cast(*node) : py::none();
            },
            py::arg("name"))
        .def("successors", &ir::Graph::successors, py::arg("id"))
        .def("predecessors", &ir::Graph::predecessors, py::arg("id"))
        .def("register_count", &ir::Graph::register_count)
        .def("edge_count", &ir::Graph::edge_count)
        .def(
            "nodes",
            [](const ir::Graph& g) {
                return py::make_iterator(g.nodes().begin(), g.nodes().end());
            },
            py::keep_alive<0, 1>());
}

void init_cdc_bindings(py::module& m) {
    py::enum_<cdc::SyncPattern>(m, "SyncPattern")
        .value("None", cdc::SyncPattern::None)
        .value("TwoFF", cdc::SyncPattern::TwoFF)
        .value("ThreeFF", cdc::SyncPattern::ThreeFF)
        .value("FourFF", cdc::SyncPattern::FourFF);

    py::class_<cdc::Finding>(m, "Finding")
        .def_readwrite("rule_id", &cdc::Finding::rule_id)
        .def_readwrite("rule_name", &cdc::Finding::rule_name)
        .def_readwrite("severity", &cdc::Finding::severity)
        .def_readwrite("source_reg_id", &cdc::Finding::source_reg_id)
        .def_readwrite("dest_reg_id", &cdc::Finding::dest_reg_id)
        .def_readwrite("source_reg_name", &cdc::Finding::source_reg_name)
        .def_readwrite("dest_reg_name", &cdc::Finding::dest_reg_name)
        .def_readwrite("source_domain", &cdc::Finding::source_domain)
        .def_readwrite("dest_domain", &cdc::Finding::dest_domain)
        .def_readwrite("reason", &cdc::Finding::reason)
        .def_readwrite("source_loc", &cdc::Finding::source_loc)
        .def_readwrite("detected_sync", &cdc::Finding::detected_sync)
        .def_readwrite("waived", &cdc::Finding::waived)
        .def_readwrite("waiver_justification", &cdc::Finding::waiver_justification)
        .def_readwrite("waiver_owner", &cdc::Finding::waiver_owner)
        .def_readwrite("bus_width", &cdc::Finding::bus_width)
        .def_readwrite("is_gray_coded", &cdc::Finding::is_gray_coded)
        .def_readwrite("has_handshake", &cdc::Finding::has_handshake);

    py::class_<cdc::CrossingAnalyzer>(m, "CrossingAnalyzer")
        .def(py::init<>())
        .def("analyze", &cdc::CrossingAnalyzer::analyze, py::arg("graph"), py::arg("domains"),
             py::arg("register_to_domain"), py::arg("num_threads") = 0);

    py::class_<cdc::PatternRecognizer>(m, "PatternRecognizer")
        .def(py::init<>())
        .def("detect_async_fifos", &cdc::PatternRecognizer::detect_async_fifos)
        .def("detect_handshakes", &cdc::PatternRecognizer::detect_handshakes)
        .def("detect_gray_encoding", &cdc::PatternRecognizer::detect_gray_encoding)
        .def("is_gray_coded", &cdc::PatternRecognizer::is_gray_coded)
        .def("is_handshake_signal", &cdc::PatternRecognizer::is_handshake_signal)
        .def("is_async_fifo_ptr", &cdc::PatternRecognizer::is_async_fifo_ptr)
        .def("analyze_and_annotate", &cdc::PatternRecognizer::analyze_and_annotate);
}

void init_clock_bindings(py::module& m) {
    py::class_<clock::ClockDomain>(m, "ClockDomain")
        .def_readwrite("id", &clock::ClockDomain::id)
        .def_readwrite("name", &clock::ClockDomain::name)
        .def_readwrite("register_ids", &clock::ClockDomain::register_ids);

    py::class_<clock::DomainResult>(m, "DomainResult")
        .def_readwrite("domains", &clock::DomainResult::domains)
        .def_readwrite("register_to_domain", &clock::DomainResult::register_to_domain)
        .def_readwrite("warnings", &clock::DomainResult::warnings);

    py::class_<clock::DomainExtractor>(m, "DomainExtractor")
        .def(py::init<>())
        .def("extract", &clock::DomainExtractor::extract);

    py::class_<clock::ClockDefinition>(m, "ClockDefinition")
        .def_readwrite("name", &clock::ClockDefinition::name)
        .def_readwrite("frequency_mhz", &clock::ClockDefinition::frequency_mhz)
        .def_readwrite("period_ns", &clock::ClockDefinition::period_ns)
        .def_readwrite("source", &clock::ClockDefinition::source)
        .def_readwrite("is_virtual", &clock::ClockDefinition::is_virtual)
        .def_readwrite("is_generated", &clock::ClockDefinition::is_generated)
        .def_readwrite("master_clock", &clock::ClockDefinition::master_clock)
        .def_readwrite("divider_ratio", &clock::ClockDefinition::divider_ratio)
        .def_readwrite("multiplier_ratio", &clock::ClockDefinition::multiplier_ratio);

    py::class_<clock::ClockConstraints>(m, "ClockConstraints")
        .def_readwrite("clocks", &clock::ClockConstraints::clocks)
        .def_readwrite("false_paths", &clock::ClockConstraints::false_paths)
        .def_readwrite("multi_cycle_paths", &clock::ClockConstraints::multi_cycle_paths)
        .def("is_false_path", &clock::ClockConstraints::is_false_path)
        .def("is_asynchronous", &clock::ClockConstraints::is_asynchronous);

    py::class_<clock::ConstraintsParser>(m, "ConstraintsParser")
        .def(py::init<>())
        .def("parse_yaml", &clock::ConstraintsParser::parse_yaml)
        .def("parse_file", &clock::ConstraintsParser::parse_file);

    py::class_<clock::SdcReader>(m, "SdcReader")
        .def(py::init<>())
        .def("read_sdc", &clock::SdcReader::read_sdc)
        .def("parse_sdc_content", &clock::SdcReader::parse_sdc_content);
}

void init_report_bindings(py::module& m) {
    py::class_<report::HtmlReportOptions>(m, "HtmlReportOptions")
        .def(py::init<>())
        .def_readwrite("output_dir", &report::HtmlReportOptions::output_dir)
        .def_readwrite("title", &report::HtmlReportOptions::title)
        .def_readwrite("include_source_snippets",
                       &report::HtmlReportOptions::include_source_snippets)
        .def_readwrite("include_summary_dashboard",
                       &report::HtmlReportOptions::include_summary_dashboard)
        .def_readwrite("dark_mode", &report::HtmlReportOptions::dark_mode)
        .def_readwrite("custom_css", &report::HtmlReportOptions::custom_css);

    py::class_<report::HtmlReporter>(m, "HtmlReporter")
        .def(py::init<>())
        .def("generate_report", &report::HtmlReporter::generate_report, py::arg("findings"),
             py::arg("options") = report::HtmlReportOptions{});
}

void init_analysis_bindings(py::module& m) {
    py::class_<analysis::Baseline>(m, "Baseline")
        .def_readwrite("name", &analysis::Baseline::name)
        .def_readwrite("timestamp", &analysis::Baseline::timestamp)
        .def_readwrite("version", &analysis::Baseline::version)
        .def_readwrite("findings", &analysis::Baseline::findings)
        .def_readwrite("total_errors", &analysis::Baseline::total_errors)
        .def_readwrite("total_warnings", &analysis::Baseline::total_warnings)
        .def_readwrite("total_waived", &analysis::Baseline::total_waived);

    py::class_<analysis::TrendReport>(m, "TrendReport")
        .def_readwrite("new_findings", &analysis::TrendReport::new_findings)
        .def_readwrite("fixed_findings", &analysis::TrendReport::fixed_findings)
        .def_readwrite("persistent_findings", &analysis::TrendReport::persistent_findings)
        .def_readwrite("total_baseline", &analysis::TrendReport::total_baseline)
        .def_readwrite("total_current", &analysis::TrendReport::total_current)
        .def_readwrite("added", &analysis::TrendReport::added)
        .def_readwrite("removed", &analysis::TrendReport::removed)
        .def_readwrite("unchanged", &analysis::TrendReport::unchanged)
        .def("improved", &analysis::TrendReport::improved)
        .def("regressed", &analysis::TrendReport::regressed)
        .def("stable", &analysis::TrendReport::stable)
        .def("summary", &analysis::TrendReport::summary);

    py::class_<analysis::TrendAnalyzer>(m, "TrendAnalyzer")
        .def(py::init<>())
        .def("save_baseline", &analysis::TrendAnalyzer::save_baseline)
        .def("load_baseline", &analysis::TrendAnalyzer::load_baseline)
        .def("compare", static_cast<analysis::TrendReport (analysis::TrendAnalyzer::*)(
                            const analysis::Baseline&, const std::vector<cdc::Finding>&)>(
                            &analysis::TrendAnalyzer::compare))
        .def("list_baselines", &analysis::TrendAnalyzer::list_baselines)
        .def("delete_baseline", &analysis::TrendAnalyzer::delete_baseline);
}

PYBIND11_MODULE(opencdc, m) {
    m.doc() = "OpenCDC Python bindings for CDC analysis";

    init_graph_bindings(m);
    init_cdc_bindings(m);
    init_clock_bindings(m);
    init_report_bindings(m);
    init_analysis_bindings(m);

    py::class_<CheckOptions>(m, "CheckOptions")
        .def(py::init<>())
        .def_readwrite("top_module", &CheckOptions::top_module)
        .def_readwrite("input_files", &CheckOptions::input_files)
        .def_readwrite("config_path", &CheckOptions::config_path)
        .def_readwrite("output_path", &CheckOptions::output_path)
        .def_readwrite("format", &CheckOptions::format)
        .def_readwrite("waiver_path", &CheckOptions::waiver_path)
        .def_readwrite("constraints_path", &CheckOptions::constraints_path)
        .def_readwrite("html_output_dir", &CheckOptions::html_output_dir)
        .def_readwrite("verbose", &CheckOptions::verbose)
        .def_readwrite("disable_rules", &CheckOptions::disable_rules)
        .def_readwrite("severity_overrides", &CheckOptions::severity_overrides)
        .def_readwrite("false_paths", &CheckOptions::false_paths);

    py::enum_<ExitCode>(m, "ExitCode")
        .value("OK", ExitCode::OK)
        .value("FINDINGS", ExitCode::FINDINGS)
        .value("INPUT_ERROR", ExitCode::INPUT_ERROR)
        .value("INTERNAL_ERROR", ExitCode::INTERNAL_ERROR);

    m.def("run", &run, py::arg("argc"), py::arg("argv"),
          "Run OpenCDC analysis from command line arguments");
}

}  // namespace python
}  // namespace opencdc

#endif  // OPENCDC_PYTHON_BINDINGS_H