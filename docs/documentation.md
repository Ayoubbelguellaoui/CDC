# OpenCDC v0.3.0 Documentation

## Table of Contents

1. [Getting Started](#getting-started)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [Features](#features)
5. [CLI Reference](#cli-reference)
6. [Configuration](#configuration)
7. [Rules Reference](#rules-reference)
8. [Waivers](#waivers)
9. [Clock Constraints](#clock-constraints)
10. [HTML Reports](#html-reports)
11. [Python Bindings](#python-bindings)
12. [LSP Server](#lsp-server)
13. [Trend Analysis](#trend-analysis)
14. [API Reference](#api-reference)

## Getting Started

OpenCDC is a static analysis tool for detecting Clock Domain Crossing (CDC) issues in RTL designs. It parses SystemVerilog/Verilog source files, builds an intermediate representation graph, and runs analysis passes to find potential CDC violations.

### Key Features

- **Semantic Pattern Recognition**: Gray-code, handshake, and async FIFO detection using semantic analysis
- **8 CDC Rules**: CDC001-CDC008 covering common CDC issues
- **Multiple Input Formats**: Verilog-2001 (`.v`) and SystemVerilog (`.sv`)
- **Flexible Configuration**: YAML config files, CLI options, waivers
- **Multiple Output Formats**: JSON, text, HTML
- **Python Bindings**: Scriptable analysis with Python
- **LSP Server**: IDE integration for real-time feedback

## Installation

### Prerequisites

- C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.28+
- Git

### Build from Source

```bash
git clone https://github.com/opencdc/opencdc.git
cd opencdc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Install

```bash
sudo cmake --install build
```

### Python Package

```bash
pip install opencdc
```

## Quick Start

### Basic Analysis

```bash
opencdc check design.sv --top top_module
```

### With Configuration

```bash
opencdc check design.sv --top top --config opencdc.yaml
```

### HTML Report

```bash
opencdc check design.sv --top top --format html --html-dir report
```

### With Constraints

```bash
opencdc check design.sv --top top --constraints constraints.sdc
```

## Features

### Semantic Pattern Recognition

OpenCDC uses semantic analysis instead of substring matching for:

- **Gray-code detection**: Analyzes XOR patterns and naming conventions
- **Async FIFO recognition**: Detects gray-coded pointer patterns
- **Handshake protocols**: Identifies valid/ready signal pairs

### Supported Rules

| ID | Name | Severity | Description |
|---|---|---|---|
| CDC001 | unsynchronized_crossing | error | Register drives register across domains without synchronization |
| CDC002 | multi_bit_crossing | error | Multi-bit bus crosses domains without gray-code or handshake |
| CDC003 | reconvergence_hazard | warning | Multiple paths from same source reconverge in destination domain |
| CDC004 | gated_clock_crossing | warning | Register clocked by gated clock crosses to another domain |
| CDC005 | muxed_clock_no_reset | warning | Register clocked by muxed clock without reset |
| CDC006 | combinational_between_sync | error | Combinational logic between synchronizer stages |
| CDC007 | missing_reset | warning | CDC register without reset signal |
| CDC008 | multi_domain_daisy_chain | warning | Signal crosses 3+ clock domains in daisy chain |

## CLI Reference

```
opencdc check <files...> [options]

Options:
  --top <module>           Top module name (required)
  --config <file>          Configuration file (YAML)
  --waiver <file>           Waiver file
  --constraints <file>      Clock constraints file (SDC or YAML)
  --format <fmt>           Output format: json, text, html (default: json)
  --out <file>              Write report to file (default: stdout)
  --html-dir <dir>          HTML report output directory (default: opencdc_report)
  --disable-rule <id>       Disable a rule (e.g., CDC001). Repeatable.
  --severity <id>=<sev>     Override rule severity (e.g., CDC003=error). Repeatable.
  --false-path <s:d>        False path (e.g., src:mod.src,dest:mod.dst). Repeatable.
  --verbose                 Enable verbose output
  --version                 Show version information
  --help                    Show help message
```

## Configuration

### YAML Config File

```yaml
rules:
  CDC001:
    enabled: true
    severity: error
  CDC003:
    enabled: false
  CDC007:
    severity: info

waivers:
  - rule: CDC001
    source: mod.src
    dest: mod.dst
    justification: "Known safe crossing"
    owner: "@team"

false_paths:
  - source: mod.src_reg
    dest: mod.meta_reg

output:
  format: json
  file: report.json
  suppress_reset_crossings: true
```

## Waivers

### Waiver File Format

```
# Substring matching (default)
CDC001 src dst clk_a clk_b "Known safe" @reviewer 2027-12-31

# Wildcard matching
WILDCARD CDC001 top.*.src_* top.*.dst_* clk_a clk_b "Module waiver" @team

# Regex matching
REGEX CDC001 top\.\w+\.src_\d+ top\.\w+\.dst_\d+ clk_a clk_b "Regex waiver" @team
```

### Waiver Fields

| Field | Required | Description |
|-------|----------|-------------|
| RULE_ID | Yes | Rule ID (e.g., CDC001) |
| SOURCE | Yes | Source register name (pattern) |
| DEST | Yes | Destination register name (pattern) |
| SOURCE_DOMAIN | Yes | Source clock domain (empty = any) |
| DEST_DOMAIN | Yes | Destination clock domain (empty = any) |
| JUSTIFICATION | Yes | Quoted reason for the waiver |
| OWNER | Yes | Who approved the waiver (e.g., @team) |
| EXPIRY | No | Expiry date (YYYY-MM-DD) |

## Clock Constraints

### SDC Format

```tcl
# Clock definitions
create_clock -name clk_core -period 10.0 [get_ports clk_core]
create_clock -name clk_periph -period 20.0

# Generated clocks
create_generated_clock -name clk_div2 -master_clock clk_core -divide_by 2 [get_pins pll/clk_out]

# False paths
set_false_path -from [get_clocks clk_core] -to [get_clocks clk_test]

# Clock groups
set_clock_groups -asynchronous -group [get_clocks clk_core] -group [get_clocks clk_periph]

# Multi-cycle paths
set_multicycle_path 2 -from [get_clocks clk_slow] -to [get_clocks clk_fast]
```

### YAML Format

```yaml
clocks:
  clk_core:
    frequency: 100
    source: pll
  clk_periph:
    frequency: 50
    divider: 2
    master_clock: clk_core

false_paths:
  - from_clock: clk_core
    to_clock: clk_test
    reason: "Test clock"

multi_cycle_paths:
  - from_clock: clk_slow
    to_clock: clk_fast
    cycles: 2

clock_groups:
  async_group:
    clocks: clk_core, clk_periph, clk_test
    asynchronous: true
```

## HTML Reports

### Generating HTML Reports

```bash
opencdc check design.sv --top top --format html --html-dir report
```

### Report Features

- **Dashboard**: Summary cards with error/warning/waived counts
- **Charts**: Severity and rule distribution bar charts
- **Filtering**: Search, severity filter, rule filter
- **Dark Mode**: Automatic dark mode support
- **Responsive**: Mobile-friendly layout

### Customizing Reports

```cpp
HtmlReportOptions options;
options.output_dir = "my_report";
options.title = "My Design Analysis";
options.include_source_snippets = true;
options.dark_mode = true;

HtmlReporter reporter;
reporter.generate_report(findings, options);
```

## Python Bindings

### Installation

```bash
pip install opencdc
```

### Usage

```python
import opencdc

# Create graph
graph = opencdc.Graph()
reg1 = graph.add_register("top.src", "clk_a", 8)
reg2 = graph.add_register("top.dst", "clk_b", 8)
graph.add_edge(reg1, reg2)

# Extract clock domains
extractor = opencdc.DomainExtractor()
domains = extractor.extract(graph)

# Analyze crossings
analyzer = opencdc.CrossingAnalyzer()
findings = analyzer.analyze(graph, domains.domains, domains.register_to_domain)

# Print findings
for f in findings:
    print(f"{f.rule_id}: {f.source_reg_name} -> {f.dest_reg_name}")
```

### Pattern Recognition

```python
recognizer = opencdc.PatternRecognizer()
recognizer.analyze_and_annotate(graph)

# Check patterns
for node in graph.nodes():
    if node.is_gray_coded:
        print(f"{node.hier_name} is gray-coded")
    if node.is_handshake_signal:
        print(f"{node.hier_name} is handshake signal")
```

### Trend Analysis

```python
analyzer = opencdc.TrendAnalyzer()

# Save baseline
analyzer.save_baseline("v1.0", findings, "baseline.json")

# Compare
baseline = analyzer.load_baseline("baseline.json")
report = analyzer.compare(baseline, current_findings)

print(report.summary())
```

## LSP Server

### Starting the Server

```bash
opencdc-lsp --port 8080 --top my_module
```

### IDE Integration

The LSP server provides real-time CDC feedback in editors that support Language Server Protocol.

#### VS Code

Add to `settings.json`:

```json
{
  "opencdc.serverPath": "opencdc-lsp",
  "opencdc.topModule": "my_module"
}
```

#### Neovim

```lua
local lsp = require('lspconfig')
lsp.opencdc.setup {
  cmd = {"opencdc-lsp", "--top", "my_module"},
  filetypes = {"verilog", "systemverilog"},
}
```

## Trend Analysis

### Saving Baselines

```bash
opencdc check design.sv --top top --out baseline.json
```

### Comparing Runs

```python
from opencdc import TrendAnalyzer

analyzer = TrendAnalyzer()
baseline = analyzer.load_baseline("baseline.json")
report = analyzer.compare(baseline, current_findings)

print(f"New: {report.new_findings}")
print(f"Fixed: {report.fixed_findings}")
print(f"Improved: {report.improved()}")
```

### Report Summary

```
Baseline: 42 findings
Current: 38 findings
New: 3, Fixed: 7
Status: IMPROVED (+4 net fixes)
Rule changes:
  CDC001: -2
  CDC002: +1
  CDC003: -1
```

## API Reference

### Graph Operations

```cpp
// Create graph
ir::Graph graph;

// Add nodes
uint64_t reg = graph.add_register("top.reg", "clk_a", 8, {"file.sv", 10, 5});
uint64_t port = graph.add_port("top.in", 8, {"file.sv", 5, 10});

// Add edges
graph.add_edge(src_id, dst_id);

// Query
const ir::Node* node = graph.find_node(id);
std::vector<uint64_t> succs = graph.successors(id);
std::vector<uint64_t> preds = graph.predecessors(id);
```

### Pattern Recognition

```cpp
cdc::PatternRecognizer recognizer;
recognizer.analyze_and_annotate(graph);

// Check patterns
bool is_gray = recognizer.is_gray_coded(node_id, graph);
bool is_handshake = recognizer.is_handshake_signal(node_id, graph);
bool is_fifo = recognizer.is_async_fifo_ptr(node_id, graph);

// Detect patterns
auto fifos = recognizer.detect_async_fifos(graph);
auto handshakes = recognizer.detect_handshakes(graph);
auto gray = recognizer.detect_gray_encoding(graph);
```

### Clock Constraints

```cpp
clock::ConstraintsParser parser;
clock::ClockConstraints constraints = parser.parse_file("constraints.sdc");

// Or parse SDC directly
clock::SdcReader reader;
constraints = reader.read_sdc("constraints.sdc");

// Query
bool is_fp = constraints.is_false_path("clk_a", "clk_b");
bool is_async = constraints.is_asynchronous("clk_a", "clk_b");
```

### HTML Reports

```cpp
report::HtmlReporter reporter;
report::HtmlReportOptions options;
options.output_dir = "report";
options.title = "CDC Analysis Report";
options.dark_mode = true;

reporter.generate_report(findings, options);
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | No unsuppressed error findings |
| 1 | One or more unsuppressed error findings |
| 2 | User/configuration/input error |
| 3 | Internal tool failure |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and contribution guidelines.

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.