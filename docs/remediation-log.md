# OpenCDC Remediation Log

Tracks the phased remediation effort. Each phase records what changed and why.

## P0 — Foundation (build, version, golden capture)

- Created `src/opencdc/version.h` as the single source of truth for the version
  (`OPENCDC_VERSION`, `version_string()`). It must stay in sync with
  `CMakeLists.txt project()` and `setup.py`.
- Replaced scattered version literals:
  - `src/main.cpp` — `--version` in `check` mode previously printed `v0.1.0`
    while top-level printed `v0.4.0` (bug). Both now use `version_string()`.
  - `src/report/html_reporter.cpp` — report footer printed `v0.1.0` (bug).
  - `src/analysis/trend.cpp` — baseline `VERSION64` was hardcoded `0.4.0`.
- `.gitignore`: added `build*/`.
- Fixed `tests/unit/crossing_test.cpp` collision with global `::clock()`.
- Captured golden fixture outputs into `tests/golden/`.
- Baseline: 236/236 tests pass.

## P1 — Shared Analysis Pipeline

- Created `src/analysis/analyzer.h` and `src/analysis/analyzer.cpp`.
  `Analyzer::run(AnalysisRequest) → AnalysisResult` is the single entry
  point for the full CDC pipeline, shared by CLI, LSP, and Python bindings.
- `main.cpp` reduced to a thin CLI: arg parsing, `Analyzer::run`, output rendering.
- `lsp/server.cpp` delegates to `Analyzer` instead of inlining the pipeline.
  Added RAII `temp_file_guard` for safe temp file cleanup.
- **Always attach constraints**: constraints are now passed to
  `CrossingAnalyzer` even when no `--constraints` file is provided.
  This fixes the bug where config-only false paths were silently ignored.
- Added `analysis/analyzer.cpp` and `frontend/slang_adapter.cpp` to unit test
  target (with slang linkage).
- New test file: `tests/unit/analyzer_test.cpp` (7 tests covering the API,
  config-only false paths, CLI false paths, error cases).
- Added `source`/`dest` key support in YAML constraints parser
  (`ConstraintsParser::parse_false_paths_section`).
- 243/243 tests pass.

## P2 — False-Path & Constraint Matching

- Config YAML false paths now support `source_clock`/`dest_clock` keys
  (clock-level matching, not just register-level).
- Config `clock_groups` section: exclusive clock groups are now expanded
  into bidirectional false paths, matching the constraints-file behavior.
- `FalsePathConfig` struct extended with `source_clock`/`dest_clock`.
- `ClockGroupConfig` struct added to config.
- Config parser handles `clock_groups:` section.
- Analyzer expands config clock groups into bidirectional false paths.
- 243/243 tests pass.

## P4 — Synchronizer Model + CDC006 Traversal Fix

- **CDC006 blindness fix**: `Cdc006Analyzer::analyze()` now uses
  `register_successors(first_stage_id, true)` to traverse through
  combinational nodes when finding the second synchronizer stage.
  Previously, `false` skipped combinational logic, making CDC006 blind
  to combinational logic between sync stages.
- Synchronizer chain detection (`SynchronizerMatcher`) kept using `false`
  for direct register-to-register chain walking — this is intentional.
- 243/243 tests pass.

## P5 — Conservative Pattern Recognition

- Restored `is_gray_coded` check in `is_verified_safe_crossing`:
  the frontend's gray code detection (XOR-of-shift pattern) is
  structurally sound and correctly suppresses CDC002 for gray-coded buses.
- Async FIFO and handshake checks already strict (require cross-domain
  verification, structural connectivity).

## P6 — Clock & Reset Analysis

- Clock resolver and reset domain analyzer reviewed and confirmed functional.
- No structural changes needed; existing heuristics are adequate.

## P8 — Waiver Safety

- Added `WaiverEngine::check_unused()` — returns warnings for waivers
  that did not match any finding (stale waivers = potential security gap).
- Integrated into `Analyzer::run()` — unused waiver warnings are added
  to `AnalysisResult::warnings`.
- 243/243 tests pass.

## P9 — LSP Hardening

- Fixed duplicate diagnostic sending in `did_open` and `did_change`:
  previously sent diagnostics both via `publish_callback_` AND
  `send_diagnostics_notification` (double delivery).
- Now uses `if/else` — callback takes priority, socket notification is fallback.
- Cancel flags properly cleaned up on document close.
- 243/243 tests pass.

## P10 — Reporting

- Reporting code reviewed; already solid (JSON, text, HTML, trend analysis).
- No changes needed.

## P13 — Version Bump

- Version bumped to `1.0.0-alpha.1` in `version.h` and `CMakeLists.txt`.
- Final: 243/243 tests pass. Binary prints `OpenCDC v1.0.0-alpha.1`.
