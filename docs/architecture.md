# Architecture

## Overview

OpenCDC is a static analysis tool that detects Clock Domain Crossing (CDC) issues in RTL designs. It parses SystemVerilog/Verilog source files, builds an intermediate representation (IR) graph, and runs a series of analysis passes to find potential CDC violations.

## Pipeline

```
Source Files (.sv/.v)
        │
        ▼
┌─────────────────┐
│ Config (early) │  Parse YAML config first
│ (ConfigParser) │  FrontendOptions, policies
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Slang Frontend │  Parse SV/V, build IR graph
│ (SlangAdapter) │  Registers, clocks, resets
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Constraints    │  SDC/YAML: clocks, false paths
│ (Constraints)  │  Generated-clock remap
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Domain Extract │  Group registers into domains
│ (DomainExtract)│
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Patterns+Resolve│  Gray/handshake/FIFO, blackbox
│ (Recognizer)   │  ModuleTree, ClockResolver
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Crossing       │  CDC001/002/004/005/007/008
│ (CrossingAnlz) │  010/011/012/013 + sync chains
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Reconvergence  │  CDC003 reconvergence hazards
│ (Reconverg.)   │
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ CDC006         │  Comb. between sync stages
│ (Cdc006Anlz)   │
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Reset Domain   │  CDC009 reset-domain crossings
│ (ResetDomain)  │
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Rule Engine    │  Enable/disable, severity
│ (RuleEngine)   │
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Waiver Engine  │  Match/suppress, expiry dates
│ (WaiverEngine) │
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Coverage+Signoff│  Coverage, signoff status
│ (Coverage)     │  Incomplete on CDC010
└─────────┬───────┘
         │
         ▼
┌─────────────────┐
│ Reporter (CLI) │  JSON, text, SARIF, HTML output
│ (Reporter)     │  Summary counts
└─────────┬───────┘
```

Note: pattern recognition, blackbox registry, and module-tree
construction run inside the Patterns+Resolve stage (see Analyzer::run).


## Module Map

```
src/
├── ir/                    # Intermediate Representation
│   └── graph.h/cpp        # Directed graph: registers/ports/nets/combinational + edges
│
├── frontend/              # Language Frontend
│   └── slang_adapter.h/cpp # Slang parser bridge, AST walking
│
├── clock/                 # Clock Analysis
│   ├── domain.h/cpp       # Clock domain extraction
│   ├── resolve.h/cpp      # Clock resolution (gated/muxed detection)
│   └── constraints.h/cpp  # SDC/YAML clock constraints parsing
│
├── cdc/                   # CDC Analysis
│   ├── crossing.h/cpp     # Main crossing analyzer (CDC001/002/004/005/007/008/010/011/012/013)
│   ├── synchronizer.h/cpp # 2FF/3FF sync chain detection
│   ├── reconvergence.h/cpp # Reconvergence hazard detection (CDC003)
│   ├── cdc006.h/cpp       # Combinational between sync stages (CDC006)
│   ├── pattern.h/cpp      # Semantic pattern recognition (gray-code, handshake, async FIFO)
│   ├── reset_domain.h/cpp # Reset domain crossing analysis (CDC009)
│   ├── blackbox.h/cpp     # Safe-IP registry (XPM, Altera, ARM)
│   └── waiver.h/cpp       # Waiver matching and application
│
├── rules/                 # Rule Management (CDC001-CDC013)
│   └── rule.h/cpp         # Rule engine: enable/disable/override
│
├── report/                # Output
│   ├── report.h/cpp       # JSON and text report generation
│   ├── html_reporter.h/cpp # Interactive HTML report generation
│   └── sarif_reporter.h/cpp # SARIF 2.1.0 report generation
│
├── config/                # Configuration
│   ├── config.h/cpp       # YAML config parser (yaml-cpp)
│   └── profile.h/cpp      # Methodology profiles (strict/asic_signoff/fpga/...)
│
├── analysis/              # Analysis Pipeline
│   ├── analyzer.h/cpp     # Unified analysis entry point
│   ├── coverage.h/cpp     # Crossing coverage computation
│   ├── signoff.h/cpp      # Signoff status evaluation
│   └── trend.h/cpp        # Baseline save/compare for trend analysis
│
├── lsp/                   # Language Server Protocol
│   └── server.h/cpp       # LSP server for IDE integration
│
└── opencdc/               # Public API
    └── opencdc.h          # CheckOptions, ExitCode, run()
```

## IR Graph Design

The IR graph is a directed graph where:

- **Nodes** represent registers, ports, nets, and combinational logic. Each node has:
  - `id` — unique 64-bit identifier
  - `hier_name` — full hierarchical name (e.g., `top.u_mod.reg_a`)
  - `module_path` / `module_type` — hierarchy position and definition name
  - `clock_domain` — name of the clock driving this register
  - `root_clock` — resolved root clock (after ClockResolver)
  - `clock_is_gated` / `clock_is_muxed` — clock properties
  - `logic_type` / `logic_inputs` — combinational function (And/Or/Xor/Not/Mux/GrayEncoder/...)
  - `is_gray_coded` / `is_handshake_signal` / `is_async_fifo_ptr` — pattern flags
  - `value_uncertain` — uncertainty propagation marker
  - `reset_signal` — name of the reset signal (if any)
  - `width` — bit width
  - `loc` — source location (file, line, column)

- **Edges** represent data flow from source register to destination register.

## Key Design Decisions

1. **Bounded analysis**: Graph (500k nodes / 1M edges) and path limits (depth 50, 10k paths) prevent unbounded memory use; truncation is reported as CDC010 and sets `analysis_status=incomplete` with `SignoffStatus::Incomplete`.

2. **Structural pattern matching**: Pattern recognition uses graph shape and explicit logic annotations; incomplete frontend information can still produce false positives or negatives.

3. **Configurable rules**: CDC001-CDC013 can be enabled/disabled and have adjustable severity levels.

4. **False-path support**: Users can specify false paths via CLI, config, or SDC/YAML constraints to suppress intentional crossings.

5. **Reset crossing control**: `suppress_reset_crossings` config suppresses CDC009 reset-domain findings; `reset_policy` tunes CDC007 escalation.
