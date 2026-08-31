# OpenCDC

Open-source static analysis tool for detecting Clock Domain Crossing (CDC) issues in RTL designs.

## Features

- Verilog/SystemVerilog parsing via [slang](https://github.com/MikePopoloski/slang)
- Verilog-2001 (`.v`) and SystemVerilog (`.sv`) support
- Clock domain inference from explicit clock ports
- Gated and muxed clock resolution to root clocks
- Register-to-register CDC crossing detection
- 2FF/3FF synchronizer recognition
- Multi-bit synchronizer misuse detection
- Reconvergence hazard detection
- Semantic pattern recognition (gray-code, async FIFO pointers, handshakes)
- Reset domain analysis (CDC009)
- Daisy-chain multi-domain tracking (CDC008)
- 10 configurable rules (CDC001-CDC010)
- False-path support (CLI, config, and SDC/YAML clock constraints)
- Waiver workflow with substring/wildcard/regex matching, auditable trail and expiry dates
- Baseline trend analysis (save/compare findings across runs)
- JSON, text, and interactive HTML reports
- LSP server for IDE integration (`opencdc lsp`)
- Python bindings (`BUILD_PYTHON_BINDINGS=ON`)
- Machine-readable output and CI-friendly exit codes

## Building

### Prerequisites

- C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.28+
- Git (for FetchContent to download slang and Google Test)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Test

```bash
ctest --test-dir build --output-on-failure
```

## Usage

```bash
# Basic check
opencdc check design.sv --top top_module

# JSON output to file
opencdc check design.sv --top top --format json --out report.json

# With config file
opencdc check design.sv --top top --config opencdc.yaml

# With waivers
opencdc check design.sv --top top --waiver waivers.txt

# Disable a rule
opencdc check design.sv --top top --disable-rule CDC001

# Override severity
opencdc check design.sv --top top --severity CDC003=error

# False path (suppress specific crossing)
opencdc check design.sv --top top --false-path src_reg:meta_reg
```

## Rules

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
| CDC009 | reset_domain_crossing | warning | Register crosses between asynchronous reset domains |
| CDC010 | path_traversal_truncated | warning | Analysis limits may have hidden additional crossings |

See [docs/rules.md](docs/rules.md) for detailed documentation.

## Config File

```yaml
rules:
  CDC001:
    enabled: true
    severity: error
  CDC003:
    enabled: false

waivers:
  - rule: CDC001, source: mod.src, dest: mod.dst, justification: "Known safe", owner: "@team"

false_paths:
  - source: mod.src_reg, dest: mod.meta_reg

output:
  format: json
  file: report.json
  suppress_reset_crossings: true
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | No unsuppressed error findings |
| 1 | One or more unsuppressed error findings |
| 2 | User/configuration/input error |
| 3 | Internal tool failure |

## Waivers

See [docs/waivers.md](docs/waivers.md) for waiver format and usage.

## CI Integration

See [docs/ci.md](docs/ci.md) for GitHub Actions, GitLab CI, and best practices.

## Documentation

- [Architecture](docs/architecture.md) — pipeline and module design
- [Rules](docs/rules.md) — rule reference and configuration
- [Waivers](docs/waivers.md) — waiver format and usage
- [CI Integration](docs/ci.md) — pipeline integration examples

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
