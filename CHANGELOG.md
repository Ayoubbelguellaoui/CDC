# Changelog

All notable changes to OpenCDC will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [0.1.0] — 2026-08-18

### Added

- **RTL Frontend**: SystemVerilog parsing via slang with register/port/edge extraction
- **Clock Domain Inference**: Automatic domain grouping from clock port names
- **Clock Resolution**: Gated and muxed clock tracing to root clocks
- **Crossing Detection**: Register-to-register CDC crossing detection (CDC001)
- **Synchronizer Recognition**: 2FF and 3FF synchronizer chain detection
- **Multi-bit Misuse Detection**: Detection of multi-bit buses used as single-bit syncs
- **Reconvergence Analysis**: Detection of fanout from same source reconverging (CDC003)
- **Rule Engine**: Configurable rules with severity overrides and enable/disable
- **Waiver Workflow**: Line-based waiver format with expiry, owner, and auditable trail
- **Reporting**: JSON array output, text summary, file output
- **CLI**: `check` command with `--top`, `--format`, `--out`, `--waiver`, `--disable-rule`, `--severity`, `--verbose`, `--version` flags
- **Exit Codes**: 0 (OK), 1 (findings), 2 (input error), 3 (internal error)
- **Documentation**: Architecture, rules reference, waivers guide, CI integration guide
- **Examples**: Basic crossing, 2FF synchronizer, waived finding
- **Tests**: 87 unit and regression tests
- **Fixtures**: 12 SystemVerilog test designs covering all scenarios
- **CI**: GitHub Actions workflow with build, test, lint, and CLI smoke tests

### Rules

| ID | Name | Severity | Version |
|---|---|---|---|
| CDC001 | unsynchronized_crossing | error | 1.0.0 |
| CDC002 | multi_bit_crossing | error | 1.0.0 |
| CDC003 | reconvergence_hazard | warning | 1.0.0 |

### Supported SystemVerilog Subset

- Module declarations with ports
- `always_ff` sequential blocks
- `logic` and `wire` declarations
- Clock and reset signals
- Basic assignments (`=`, `<=`)
- Gated clock expressions (`&`, `|`, ternary `? :`)

### Known Limitations

- No async FIFO recognition
- No gray-code counter recognition
- No handshake protocol recognition
- No combinational logic between sync stages detection
- No multi-cycle path constraints
- No hierarchical module instantiation analysis
