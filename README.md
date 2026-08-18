# OpenCDC

Open-source static analysis tool for detecting Clock Domain Crossing (CDC) issues in RTL designs.

## Features

- Verilog/SystemVerilog parsing via [slang](https://github.com/MikePopoloski/slang)
- Clock domain inference from explicit clock ports
- Gated and muxed clock resolution to root clocks
- Register-to-register CDC crossing detection
- 2FF/3FF synchronizer recognition
- Multi-bit synchronizer misuse detection
- Reconvergence hazard detection
- Configurable rule engine with severity overrides
- Waiver workflow with auditable trail
- Machine-readable JSON and text output
- CI-friendly exit codes

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

# Text output
opencdc check design.sv --top top --format text

# With waivers
opencdc check design.sv --top top --waiver waivers.txt

# Disable a rule
opencdc check design.sv --top top --disable-rule CDC001

# Override severity
opencdc check design.sv --top top --severity CDC003=error
```

## Rules

| ID | Name | Severity | Description |
|---|---|---|---|
| CDC001 | unsynchronized_crossing | error | Register drives register across domains without synchronization |
| CDC002 | multi_bit_crossing | error | Multi-bit bus crosses domains without gray-code or handshake |
| CDC003 | reconvergence_hazard | warning | Multiple paths from same source reconverge in destination domain |

See [docs/rules.md](docs/rules.md) for detailed documentation.

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
