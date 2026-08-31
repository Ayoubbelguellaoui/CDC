# Architecture

## Overview

OpenCDC is a static analysis tool that detects Clock Domain Crossing (CDC) issues in RTL designs. It parses SystemVerilog/Verilog source files, builds an intermediate representation (IR) graph, and runs a series of analysis passes to find potential CDC violations.

## Pipeline

```
Source Files (.sv/.v)
        │
        ▼
┌─────────────────┐
│  Slang Frontend  │  Parse SV/V via slang, build IR graph
│  (SlangAdapter)  │  Extract registers, clocks, resets, edges
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Domain Extract  │  Group registers into clock domains
│  (DomainExtractor)│
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Clock Resolver  │  Trace gated/muxed clocks to root
│  (ClockResolver) │  Detect clock properties
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Crossing Analysis│  Detect CDC001/002/004/005/007/008/010
│(CrossingAnalyzer)│  Check for 2FF/3FF sync chains
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Reconvergence   │  Detect CDC003 reconvergence hazards
│  (Reconvergence) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  CDC006 Analysis │  Detect combinational logic between
│  (Cdc006Analyzer)│  synchronizer stages
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Rule Engine     │  Apply rule overrides (enable/disable/
│  (RuleEngine)    │  severity changes)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Waiver Engine   │  Match and suppress waived findings
│  (WaiverEngine)  │  Support expiry dates
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Reporter        │  JSON or text output
│  (Reporter)      │  Summary counts
└─────────────────┘
```

## Module Map

```
src/
├── ir/                    # Intermediate Representation
│   ├── graph.h/cpp        # Directed graph: nodes (registers/ports) + edges
│
├── frontend/              # Language Frontend
│   ├── slang_adapter.h/cpp # Slang parser bridge, AST walking
│
├── clock/                 # Clock Analysis
│   ├── domain.h/cpp       # Clock domain extraction
│   └── resolve.h/cpp      # Clock resolution (gated/muxed detection)
│
├── cdc/                   # CDC Analysis
│   ├── crossing.h/cpp     # Main crossing analyzer (CDC001/002/004/005/007/008)
│   ├── synchronizer.h/cpp # 2FF/3FF sync chain detection
│   ├── reconvergence.h/cpp # Reconvergence hazard detection (CDC003)
│   ├── cdc006.h/cpp       # Combinational between sync stages (CDC006)
│   └── waiver.h/cpp       # Waiver matching and application
│
├── rules/                 # Rule Management
│   └── rule.h/cpp         # Rule engine: enable/disable/override
│
├── report/                # Output
│   └── report.h/cpp       # JSON and text report generation
│
├── config/                # Configuration
│   └── config.h/cpp       # YAML-like config parser
│
└── opencdc/               # Public API
    └── opencdc.h          # CheckOptions, ExitCode, run()
```

## IR Graph Design

The IR graph is a directed graph where:

- **Nodes** represent registers and ports. Each node has:
  - `id` — unique 64-bit identifier
  - `hier_name` — full hierarchical name (e.g., `top.u_mod.reg_a`)
  - `clock_domain` — name of the clock driving this register
  - `root_clock` — resolved root clock (after ClockResolver)
  - `clock_is_gated` / `clock_is_muxed` — clock properties
  - `reset_signal` — name of the reset signal (if any)
  - `width` — bit width
  - `loc` — source location (file, line, column)

- **Edges** represent data flow from source register to destination register.

## Key Design Decisions

1. **Bounded analysis**: Graph and path limits prevent unbounded memory use; truncation is reported as CDC010 or an incomplete-analysis error.

2. **Structural pattern matching**: Pattern recognition uses graph shape and explicit logic annotations; incomplete frontend information can still produce false positives or negatives.

3. **Configurable rules**: CDC001-CDC010 can be enabled/disabled and have adjustable severity levels.

4. **False-path support**: Users can specify false paths via CLI or config to suppress intentional crossings.

5. **Reset crossing control**: Config option to suppress CDC001 findings when the destination register has a reset signal.
