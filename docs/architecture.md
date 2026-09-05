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
│ Reset Domain     │  Detect CDC009 reset domain crossings
│ (ResetDomainAnalyzer)│
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
│   ├── synchronizer.h/cpp # 2FF/3FF sync chain detection (width-validated)
│   ├── pattern.h/cpp      # Semantic pattern recognizer (gray-code, handshake, async FIFO)
│   ├── reconvergence.h/cpp # Reconvergence hazard detection (CDC003)
│   ├── cdc006.h/cpp       # Combinational between sync stages (CDC006)
│   ├── reset_domain.h/cpp # Reset domain crossing detection (CDC009)
│   └── waiver.h/cpp       # Waiver matching and application
│
├── rules/                 # Rule Management
│   └── rule.h/cpp         # Rule engine: enable/disable/override
│
├── report/                # Output
│   ├── report.h/cpp       # JSON and text report generation
│   └── html_reporter.h/cpp # HTML report generation
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

1. **Bounded analysis**: Graph and path limits prevent unbounded memory use; truncation is reported as CDC010 and sets `analysis_status` to `incomplete`.

2. **Structural pattern matching**: Pattern recognition uses graph shape and explicit logic annotations (via `PatternRecognizer`); naming alone is not sufficient evidence for safety.

3. **Configurable rules**: CDC001-CDC010 can be enabled/disabled and have adjustable severity levels.

4. **False-path support**: Users can specify false paths via CLI or config to suppress intentional crossings.

5. **Reset crossing control**: Config option to suppress CDC009 findings when the destination register has a reset signal.

## Safety Provenance Model

Every finding carries a `SafetyStatus` and `safety_provenance` string explaining how safety was determined.

### SafetyStatus Values

| Status | Meaning |
|--------|---------|
| `VerifiedSafe` | Structural evidence confirms crossing is safe (sync chain, gray code, handshake, or async FIFO). |
| `VerifiedUnsafe` | Structural evidence confirms crossing is unsafe (no synchronizer). |
| `Candidate` | Heuristic suggests potential issue, but insufficient structural evidence. |
| `Ambiguous` | Cannot determine safety (e.g., reset domain conflict with no clear safe/unsafe). |
| `Unknown` | No safety classification applied. |

### Provenance Strings

Provenance is a human-readable description of the evidence chain:

- `"synchronizer:TwoFF"` — 2FF synchronizer chain detected
- `"synchronizer:ThreeFF"` — 3FF synchronizer chain detected
- `"gray_coded"` — structural gray encoder/decoder pair verified
- `"handshake"` — valid/ready handshake pair verified
- `"no_reset"` — both registers lack reset signal
- `"gated_clock"` — register clocked by gated clock
- `"muxed_clock"` — register clocked by muxed clock
- `"reset_domain_conflict"` — different reset domains
- `"reconvergence"` — multi-path reconvergence detected
- `"no_synchronizer"` — cross-domain path without sync chain

### Population Rules

1. **CDC001**: Safety status is `VerifiedSafe` when a sync chain exists (provenance: `"synchronizer:TwoFF"` or `"synchronizer:ThreeFF"`). Severity is downgraded to warning. Otherwise `VerifiedUnsafe` with severity error.
2. **CDC002**: Safety status is `VerifiedSafe` when structural gray code, handshake, or async FIFO is verified. Otherwise `VerifiedUnsafe`.
3. **CDC003**: Safety status is always `Candidate` (heuristic).
4. **CDC004/005**: Safety status is `VerifiedUnsafe` (clock property makes crossing unsafe).
5. **CDC006**: Safety status is `VerifiedUnsafe` (combinational logic in sync chain).
6. **CDC007**: Safety status is `VerifiedUnsafe` when both registers lack reset. `Candidate` when only one lacks reset.
7. **CDC008**: Safety status is `Candidate` (multi-domain chain).
8. **CDC009**: Safety status is `Ambiguous` (reset domain conflict). Severity suppressed to info when `suppress_reset_crossings` is enabled.
9. **CDC010**: Safety status is `Unknown` (diagnostic only).
