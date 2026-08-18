# Architecture

OpenCDC is a static analysis tool for detecting Clock Domain Crossing (CDC) issues in RTL designs.

## Pipeline

```
RTL Input (.sv/.v)
       │
       ▼
┌─────────────────┐
│  slang Frontend  │  Parse, elaborate, extract registers/ports/edges
└────────┬────────┘
         │  ir::Graph
         ▼
┌─────────────────┐
│ Clock Resolver   │  Trace gated/muxed clocks to root clock
└────────┬────────┘
         │  ir::Graph (root_clock populated)
         ▼
┌─────────────────┐
│ Domain Extractor │  Group registers by clock domain
└────────┬────────┘
         │  vector<ClockDomain>
         ▼
┌─────────────────┐
│ Crossing Analyzer│  Find register→register edges crossing domains
└────────┬────────┘
         │  vector<Finding>
         ▼
┌─────────────────┐
│ Reconvergence    │  Detect fanout from same source reconverging
└────────┬────────┘
         │  + CDC003 findings
         ▼
┌─────────────────┐
│ Rule Engine      │  Apply severity overrides, enable/disable rules
└────────┬────────┘
         │  filtered findings
         ▼
┌─────────────────┐
│ Waiver Engine    │  Match waivers, mark findings as waived
└────────┬────────┘
         │  waived findings
         ▼
┌─────────────────┐
│ Reporter         │  JSON array, text summary, file output
└─────────────────┘
```

## Modules

| Module | Files | Responsibility |
|---|---|---|
| **Frontend** | `src/frontend/slang_adapter.*` | Parse SystemVerilog via slang, build graph |
| **IR** | `src/ir/graph.*` | Directed graph: Nodes (registers, ports) + Edges |
| **Clock** | `src/clock/domain.*`, `src/clock/resolve.*` | Domain extraction, gated clock resolution |
| **CDC** | `src/cdc/crossing.*`, `src/cdc/synchronizer.*`, `src/cdc/reconvergence.*`, `src/cdc/waiver.*` | Core analysis: crossings, sync chains, reconvergence, waivers |
| **Rules** | `src/rules/rule.*` | Rule definitions, severity overrides, enable/disable |
| **Report** | `src/report/report.*` | JSON/text output, summary, file writing |

## Graph Model

- **Nodes**: `Register` (with clock_domain, root_clock, width, reset) or `Port`
- **Edges**: Directed register→register or register→port connectivity
- Source locations preserved for all register nodes
- `root_clock` populated by ClockResolver for gated/muxed clock designs

## Data Flow

1. `SlangAdapter::elaborate()` → `FrontendResult` (graph + clock/reset info)
2. `ClockResolver::resolve()` → mutates graph nodes with root_clock
3. `DomainExtractor::extract()` → `DomainResult` (domains + warnings)
4. `CrossingAnalyzer::analyze()` → `vector<Finding>` (CDC001, CDC002)
5. `ReconvergenceAnalyzer::analyze()` → `vector<Finding>` (CDC003)
6. `RuleEngine::filter()` → apply severity/enable overrides
7. `WaiverEngine::apply()` → mark waived findings
8. `Reporter` → output to stdout/file
