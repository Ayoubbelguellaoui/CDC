# Contributing to OpenCDC

Thank you for your interest in contributing to OpenCDC.

## Getting Started

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make your changes
4. Run tests (`ctest --test-dir build --output-on-failure`)
5. Submit a pull request

## Development Setup

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j$(nproc)

# Test
ctest --test-dir build --output-on-failure
```

## Code Style

- C++20, no compiler extensions
- Google Test for unit tests
- Each finding must have a deterministic reason and rule ID
- Format code with `clang-format` before committing

## Project Structure

```
src/
  frontend/   — slang parser integration
  ir/         — graph representation (Node, Edge, Graph)
  clock/      — domain extraction, clock resolution
  cdc/        — crossing, synchronizer, reconvergence, waiver analysis
  rules/      — rule engine, configuration
  report/     — JSON/text output
tests/
  unit/       — isolated component tests
  regression/ — end-to-end RTL fixture tests
  fixtures/sv/ — SystemVerilog test designs
docs/         — architecture, rules, waivers, CI documentation
```

## Adding a CDC Rule

1. Add a test fixture in `tests/fixtures/sv/`
2. Add expected behavior in `tests/regression/frontend_test.cpp`
3. Implement the check in `src/cdc/`
4. Add unit tests in `tests/unit/`
5. Register the rule in `src/rules/rule.cpp`
6. Document in `docs/rules.md`

## Adding a Fixture

Fixtures are small SystemVerilog designs that exercise specific CDC scenarios:

```systemverilog
module my_fixture (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);
    // Your CDC scenario here
endmodule
```

Then add a regression test in `tests/regression/frontend_test.cpp` that verifies the expected findings.

## Testing

- **Unit tests**: Test individual components in isolation
- **Regression tests**: End-to-end tests using RTL fixtures
- **E2E tests**: Verify CLI output, exit codes, and report format

All tests must pass before submitting a PR.
