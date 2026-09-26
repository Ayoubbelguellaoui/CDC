**Changelog**
All notable changes to OpenCDC will be documented in this file.

**[Unreleased]**

**Added**
- **Realistic benchmark corpus** (`benchmarks/corpus/`, 16 designs, 18 manifest entries, manifest v1.2): correct designs, known violations, gray/binary async FIFOs, 4-phase + 2-phase + pulse handshakes, generated/gated/muxed clocks, 3-level hierarchy, blackbox vendor IP (A/B entries with/without model), and FP-prone tricky cases. All 25 fixtures pass (F1=1.000); 6 new `known_gaps` document verified limitations (wide-chain sync, handshake payload, instance-to-instance tracing, CDC006 reachability, reset-sync depth, generated-clock resolution). Runner gains per-entry `config`/`waiver`/`constraints`/`args`, rule-level `present_rules`/`absent_rules`, `reason_contains` matching, and fails fixtures when the tool itself errors instead of vacuous-passing on empty output.

**Fixed**
- **Config native booleans**: `enabled: true/false` (unquoted) and other boolean keys now parse as native YAML bools. Previously `as<std::string>()` threw `BadConversion` and the parser silently fell back to the legacy parser, dropping the setting. Also accepts `yes/no/on/off/1/0`.
- **Config `format: sarif`**: accepted again (allowlist now `json/text/html/sarif`, matching CLI).
- **Config new keys**: `reconvergence_depth` (1-32), `min_sync_stages` (2-5), `require_structural_proof`, `allow_user_annotation`, `reset_policy`, `multicycle_path_policy`, `blackboxes`, and top-level `suppress_reset_crossings` are now parsed from YAML.
- **CLI `--format` precedence**: config `output.format` only applies when `--format` was not explicitly passed (`format_explicit` flag). Explicit `--format json` is no longer hijacked by config.
- **Exit code**: `has_unsuppressed_errors()` now skips `false_path`/`multicycle`-suppressed errors, matching `count()`. Suppressed errors no longer cause exit 1.
- **`--jobs` cap**: CLI clamps to 64 threads; `parallel_for`/`parallel_map`/`ThreadPool` enforce the same hard cap in addition to the existing `min(jobs, items)` cap.
- **Waiver ReDoS**: regex waivers capped at 256 chars / 10 quantifiers, nested quantifiers `(q)+` rejected at load and add time, compiled with `optimize`.
- **LSP**: `did_change` no longer orphans cancel flags (analyze owns flag creation/reuse); empty-content requests no longer read arbitrary `file://` paths from disk (uses open-document text or returns `empty-document`); loopback check accepts full `127.0.0.0/8`; temp files use `fchmod(0600)` instead of process-global `umask`; setters snapshotted under lock.
- **Waiver ReDoS follow-up**: quantified alternations (`(a|aa)+`), `?`-nested groups (`(a+)?`), and non-capturing prefix handling; `(?:abc)+` still accepted.
- **Config**: `clock_groups.exclusive` typos now fail the file (was silently `true`); all `blackboxes.*` bools fail closed consistently; unknown top-level keys rejected (`Unknown config key`).
- **LSP races**: `server_loop` snapshots `bind_address`/`allow_remote` under lock; client accept check-and-set under `socket_mutex_`; `start()` on a running server refuses instead of deadlocking; dead `uri_decode_path` removed; empty-document refusal symmetric for remote; dead-peer write failure drops the client instead of stopping the server.
- **LSP framing (flaky-test root cause)**: client `send_request` re-scans buffered data after skipping a notification instead of blocking in `read()` while a coalesced response sits unprocessed (was a ~50% 30s-hang); server treats read-timeout `EAGAIN` as idle-keep-waiting instead of dropping the client; response-write failure drops only that client; test client uses `MSG_NOSIGNAL` so a closed peer surfaces as an error, never SIGPIPE.
- **Analyzer**: `run_incremental` re-runs analysis stages on the mutated graph (was re-elaborating from files); config parsed before elaboration so `allow_user_annotation` reaches the frontend.
- **Frontend**: skipped unknown-clock blocks emit warnings (were silent); `Net→Register` promotion preserves width/location and ORs flags; continuous-assign uses declared LHS width; scope nesting capped at 64; clock-trace cache keyed by graph identity+generation+size with deterministic leaf tie-break.
- **SARIF**: tool version from `OPENCDC_VERSION`; `startLine` clamped to ≥1; run `properties` now carry methodology/signoff/coverage counts.
- **Reports**: text `handshake` unified to `handshake_controlled` (matches JSON).
- **Python**: `run()` takes an argv list; `CheckOptions` exposes threads/profile/defines/signoff/baselines; `compare_with_file` bound.
- **Benchmarks**: dict-shaped CLI output handled, per-fixture timeouts recorded, FP no longer counted as FN, manifest/fixture paths validated.
- **CLI**: `--out`+html warns; `--compare-baseline` documents report skip and warns on `--save-baseline`; `save_baseline` failures warn; usage text updated.
- **Docs**: `rules.md`/`rule-semantics.md` aligned to code (CDC001 info, CDC003 both-paths, CDC004/005 intermediate-path scope, CDC006 cross-domain feed, CDC007 either-empty); CDC009–CDC013 documented.
- **setup.py**: README resolved relative to the file; version regex tolerant; classifier matches `python_requires`; CMake >= 3.28 enforced; parallel native build.
- **Remains round 2**: HTML artifacts written atomically (temp+rename) with sorted `findings.html` order, no `:0` locations, and remote CSS directives stripped; `--out` report and baseline saves atomic; SARIF `uri` is a proper `file://`/relative URI; `publish_callback_` race closed; constraints size-cap `-1` guards; coverage dead code removed; CI smoke asserts exact exit codes + SARIF shape.
- **Not-done round**: release SBOM (SPDX) + keyless cosign signing + `id-token:write`; all GH Actions pinned to SHAs; Python bindings compile-verified (fixed `add_edge`/`is_false_path` overload ambiguity + `module_path`/`module_type` arg annotations), GIL released on long calls, full `module.cpp` object compile green; my lines clang-format-clean under repo style; `LspClient` framing rework (drain-buffer-first) + server `EAGAIN`-keep-waiting + per-client write-failure isolation.
- **Still round**: `--incdir` must exist and be a directory, `--define` must be `NAME[=value]` (fail-fast errors instead of cryptic slang failures); gray transforms through single-level function calls recognized (`gray <= bin2gray(bin)` no longer fires CDC002); benchmark runner fixed + manifests re-baselined to verified behavior (**8/8 fixtures, F1=1.000**, was 5/8) with stale "not implemented" gaps replaced by two measured ones; ambiguous controls reported without failing; Windows portability (`socket_compat.h`, `temp_file.h`, MSVC includes, Windows CI job).
- **Standing finish**: root-caused the 3 stress regressions to an over-broad operand-collection change (output-port `Assignment` expressions) via IR dumps — rescoped the fix to call-actual mapping only, restoring greens; extended gray recognition to nested calls + task outputs (CDC002 gone on all forms, verified by probes); resolver ambiguous-name tie-breaks; all findings re-verified against fixture intent.
- **Remains round**: HTML `custom_css` strips `@import`/`url()` exfiltration vectors and empty output dirs throw; clock event names normalized (inner whitespace removed, trailing `[N]` selects stripped, `gen[0].clk` kept distinct); `RuleEngine::is_enabled` returns false for unknown rules (findings still fail-visible); crossing coverage enumerates via `find_register_paths` with dedup like the analyzer; config preserves original case in errors and rejects unreadable-size files; `ThreadSafeQueue` gains blocking `wait_pop`/`shutdown`; CLI reports missing option values, rejects unknown `lsp` flags, splits `--false-path` on the last colon, and errors on missing baselines; report paths relativized under cwd; `architecture.md` pipeline/modules/IR/decisions updated; CI smoke asserts exact exit codes plus SARIF shape; `LspClient` uses `MSG_NOSIGNAL`.

**[0.4.1] — 2026-09-21**

**Fixed**
- **LSP always available**: Removed `#ifdef OPENCDC_ENABLE_LSP` guards — LSP server is now always compiled in.
- **LSP `wait()` missing**: Added `LspServer::wait()` method so the CLI `lsp` subcommand blocks correctly.
- **`--jobs` flag**: New CLI flag to control parallel analysis threads (wired through to `CrossingAnalyzer`).
- **`--save-baseline` / `--compare-baseline`**: New CLI flags for trend analysis — save findings baseline or compare against a previous run and exit.
- **Test CMakeLists**: Unit and regression tests now link `opencdc_core` instead of recompiling all source files (fixes LTO and ODR violations).
- **Dead declarations removed**: `HtmlReporter::write_index_html`, `write_findings_html`, `write_summary_html` removed (never implemented).

**Changed**
- **yaml-cpp dependency**: Config and constraints YAML parsing rewritten to use [yaml-cpp](https://github.com/jbeder/yaml-cpp) (0.8.0) instead of hand-rolled line-by-line parsers. Old compact one-line waiver/false-path format still supported for backward compatibility.
- **Config file format**: Config YAML now uses proper nested maps for waivers, false_paths, and clock_groups. Old compact comma-separated format is still accepted.
- **Constraints YAML format**: Constraints YAML now uses proper nested maps for false_paths, multi_cycle_paths, and clock_groups. Old compact format still accepted.

**Performance**
- **`get_root_port_name`**: O(N) graph scan replaced with O(1) `short_to_hier_` index lookup.
- **`find_domain`**: Added O(1) overload using `register_to_domain` map (linear scan fallback retained for callers without the map).

**[0.3.1] — 2026-08-21**

**Fixed**
- **CDC001 Sync-chain downgrade**: CDC001 now downgrades to warning when 2FF/3FF synchronizer detected. Derived rules (CDC002/004/005/007) suppressed when synced.
- **CDC008 threshold**: Fixed off-by-one (4→3 domains).
- **CDC006 severity**: Changed from warning to error.
- **Handshake dual-flag**: LogicType-primary classification; ambiguous nodes (only is_handshake_signal) skipped with warning.
- **Reset domain polarity**: Reset domain comparison now checks both name and polarity.
- **Constraints is_false_path**: Empty-clock short-circuit no longer matches everything; requires register match.
- **Constraints bidirectional**: `is_asynchronous`/`get_clock` use exact match instead of substring.
- **RuleEngine severity preservation**: Analyzer-set severity not overridden unless user explicitly configures it.
- **Thread safety**: PatternRecognizer mutable caches removed (const_cast eliminated). WaiverEngine regex pre-compiled at add time. LSP cancel_flags use shared_ptr to prevent iterator invalidation. ThreadPool rejects submit-after-shutdown and catches worker exceptions.
- **Crash robustness**: Trend analyzer `stoul`/`stoi` calls guarded with try-catch. LSP path traversal defense strengthened.
- **Locale safety**: `ConfigParser::to_lower` uses `unsigned char` cast instead of `::tolower`.

**[0.3.0] — 2026-08-18**

**Added**
- **Semantic Pattern Recognition**: Gray-code, handshake, and async FIFO detection using semantic analysis instead of substring matching
- **Async FIFO Recognition**: Automatic detection of async FIFO patterns with gray-coded pointers
- **Enhanced SystemVerilog Support**: Support for `always_comb`, continuous assignments, and improved muxed clock detection
- **Combinational Logic Tracking**: IR graph now tracks logic types (AND, OR, XOR, MUX) between registers
- **Regex-Based Waivers**: Support for wildcard (`*`, `?`) and full regex patterns in waiver files
- **Clock Constraints File**: SDC and YAML constraint file support for clock definitions and false paths
- **HTML Report Generator**: Interactive HTML reports with dashboard, charts, and filtering
- **Reset Domain Analysis**: Detection of reset domain crossings between different reset signals
- **Trend Analysis**: Baseline comparison and trend tracking across runs
- **Parallel Analysis**: Thread pool and parallel processing utilities for large designs
- **Pattern Recognizer Module**: Centralized pattern detection for gray-code, handshake, and async FIFO patterns

**Changed**
- **CDC002 Detection**: Now uses semantic analysis instead of substring matching for gray-code/handshake detection
- **Waiver Matching**: Improved matching algorithm with support for substring, wildcard, and regex modes
- **IR Graph**: Added `LogicType` enum and pattern recognition flags to Node structure
- **Crossing Analyzer**: Integrated pattern recognizer for improved CDC002 detection

**[0.2.0] — 2026-08-18**

**Added**
- **CDC002 Detection**: Multi-bit bus crossing detection with gray-code and handshake heuristics
- **CDC004 Rule**: Gated clock crossing detection
- **CDC005 Rule**: Muxed clock without reset detection
- **CDC006 Rule**: Combinational logic between sync stages detection
- **CDC007 Rule**: Missing reset on CDC registers detection
- **CDC008 Rule**: Multi-domain daisy chain detection (3+ domains)
- **Config File**: YAML-like config file support for rules, waivers, and output settings
- **Performance**: Graph adjacency lists for O(1) successor/predecessor lookup
- **Performance**: Domain register-to-domain reverse map for O(1) domain lookup
- **API**: `Graph::find_node_mutable()` for safe mutable access
- **CI**: Coverage reporting with lcov and Codecov integration
- **CI**: ASan + UBSan sanitizer builds in CI matrix
- **Width Extraction**: Frontend now extracts actual register bit widths from SV declarations

**Fixed**
- Format default inconsistency between CLI and header
- Removed `const_cast` hack in slang adapter
- O(N*M) domain lookup replaced with O(1) reverse map
- O(E) successor/predecessor lookup replaced with O(1) adjacency lists

**[0.1.0] — 2026-08-18**

**Added**
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
- **CLI**: check command with --top, --format, --out, --waiver, --disable-rule, --severity, --verbose, --version flags
- **Exit Codes**: 0 (OK), 1 (findings), 2 (input error), 3 (internal error)
- **Documentation**: Architecture, rules reference, waivers guide, CI integration guide
- **Examples**: Basic crossing, 2FF synchronizer, waived finding
- **Tests**: 87 unit and regression tests
- **Fixtures**: 12 SystemVerilog test designs covering all scenarios
- **CI**: GitHub Actions workflow with build, test, lint, and CLI smoke tests

**Rules**
| | | | |
|-|-|-|-|
| **ID** | **Name** | **Severity** | **Version** |
| CDC001 | unsynchronized_crossing | error | 1.0.0 |
| CDC002 | multi_bit_crossing | error | 1.0.0 |
| CDC003 | reconvergence_hazard | warning | 1.0.0 |
| CDC004 | gated_clock_crossing | warning | 1.0.0 |
| CDC005 | muxed_clock_no_reset | warning | 1.0.0 |
| CDC006 | combinational_between_sync | error | 1.0.0 |
| CDC007 | missing_reset | warning | 1.0.0 |
| CDC008 | multi_domain_daisy_chain | warning | 1.0.0 |

**Supported SystemVerilog Subset**
- Module declarations with ports
- always_ff sequential blocks
- logic and wire declarations
- Clock and reset signals
- Basic assignments (=, <=)
- Gated clock expressions (&, |, ternary ? :)

**Known Limitations**
- No async FIFO recognition
- No gray-code counter recognition
- No handshake protocol recognition
- No combinational logic between sync stages detection
- No multi-cycle path constraints
- No hierarchical module instantiation analysis
