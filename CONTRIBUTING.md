# Contributing to OpenCDC

## Development Setup

### Prerequisites

- C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.28+
- Git

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
```

### Test

```bash
ctest --test-dir build --output-on-failure
```

## Code Structure

See [docs/architecture.md](docs/architecture.md) for the full module map.

## Adding a New Rule

1. Define the rule in `src/rules/rule.cpp` (add to `register_default_rules`)
2. Create an analyzer class in `src/cdc/` (e.g., `cdc009.h`, `cdc009.cpp`)
3. Wire it into the pipeline in `src/main.cpp`
4. Add unit tests in `tests/unit/`
5. Add regression tests in `tests/regression/`
6. Update `docs/rules.md`

## Code Style

- C++20 standard
- Namespaces: `opencdc::ir`, `opencdc::clock`, `opencdc::cdc`, etc.
- Classes: PascalCase (e.g., `CrossingAnalyzer`)
- Methods: snake_case (e.g., `find_domain_for_node`)
- Files: snake_case (e.g., `crossing.cpp`)
- Run `clang-format` before committing

## Pull Request Process

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make changes with tests
4. Run `ctest --test-dir build --output-on-failure` — all tests must pass
5. Run `clang-format --dry-run --Werror src/` — no format violations
6. Submit a pull request with a clear description

## Reporting Issues

Use GitHub Issues. Include:
- RTL snippet that triggers the issue
- Expected vs actual behavior
- OpenCDC version and build configuration
