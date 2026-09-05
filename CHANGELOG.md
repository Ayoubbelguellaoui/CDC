**Changelog**
All notable changes to OpenCDC will be documented in this file.

**[0.4.0] — 2026-09-05**

**Fixed**
- **CDC006 multi-stage fix**: Findings for 3FF+ chains now correctly report the actual stage pair with combinational logic, not always the first stage. Also detects combinational predecessors behind combinational nodes.
- **CDC009 safety status**: Reset domain crossing findings now include `safety_status` and `safety_provenance` fields (was: Unknown/empty, invisible in safety reports).
- **HTML safety-status filter**: Fixed CSS selector mismatch (`.safety-status` → `.safety-badge`) making the safety-status dropdown filter functional.
- **CDC001 misleading reason**: Reason string now says "with X detected" when a synchronizer IS found, instead of always saying "without synchronization."
- **CDC007 dead code**: Removed unreachable `else` branch in reason string ternary.
- **Synchronizer dedup key**: `match()` deduplication now includes destination register ID, preventing multiple destinations from same source being collapsed.
- **SynchronizerChain depth**: Depth is now computed from `stage_ids.size()` after chain walk, not hardcoded.
- **Synchronizer strict mode**: `match()` chain walk now applies the same strict predecessor validation as `find_pattern_for_dest`, preventing inconsistent chain topologies.
- **CDC001 ambiguous state**: Sync chains with structural warnings (reset polarity mismatch, fanout) now get `SafetyStatus::Ambiguous` instead of `VerifiedSafe`.
- **CDC007 mixed reset**: Crossings where one register has reset and the other doesn't now emit CDC007 at `info` severity (was: silently unreported).
- **Synchronizer warnings**: `has_chain_warnings()` method added for per-destination chain health check.
- **CDC006 between stages**: Now fires on ANY combinational logic feeding a sync stage (not just cross-domain-driven), as unexpected combinational inputs defeat synchronizer purpose.
- **Synchronizer width validation**: 2FF/3FF synchronizer detection now requires all stages to be single-bit (width=1). Multi-bit buses through synchronizer chains are no longer classified as safe crossings.
- **CDC007 false positives**: CDC007 now fires as warning only when BOTH source and destination registers lack a reset signal (was: either one).
- **ClockResolver integration**: Main analysis pipeline now calls `ClockResolver::resolve()` to detect gated/muxed clocks through the IR graph, not just from frontend flags.
- **Analysis status tracking**: `AnalysisResult` now includes `analysis_status` field ("complete", "incomplete", or "failed") on all exit paths.
- **Safety provenance**: All findings now carry `safety_status` and `safety_provenance` fields explaining how safety was determined.
- **Muxed clock warning dedup**: Muxed clock warnings no longer share a `seen_clocks` set with domain resolution, preventing silent suppression.
- **Synchronizer depth tracking**: 4+ stage synchronizer chains (e.g., src→meta→sync1→sync2→dst) now correctly detected as 3FF/4FF patterns instead of hardcoded 3-stage limit.
- **CDC006 all stage pairs**: Combinational logic detection now checks ALL adjacent stage pairs in a sync chain, not just the second-to-second stage transition.
- **CDC006 before-first-stage**: Also detects combinational logic between the cross-domain source and the first sync stage.
- **CDC003 single-bit reconvergence**: Single-bit sources through independent 2FF sync chains no longer trigger CDC003.
- **CDC003 BFS truncation warning**: BFS hop/node limit breach now appends a truncation notice to the CDC003 reason string.
- **Report summary format**: Summary now includes `analysis_status` field in structured `key=value` format.

**Added**
- **Adversarial test suite**: 18+ new tests proving naming alone does not bypass detection rules (sync, gray-code, handshake, async FIFO, CDC007).
- **Systematic CDC mutation tests**: 15 new mutation tests (`cdc_mutation_test.cpp`) covering golden sync, missing stage, combinational insertion, multi-bit bus, missing reset, gated clock, reconvergence, safety status, naming bypass, muxed clock, daisy chain, reset domain crossing, mixed reset, and thread determinism.
- **Thread determinism test**: Verifies identical findings regardless of thread count (1, 2, 4) with complex topology.
- **Analysis status tests**: Validates analysis_status "complete" and "failed" states.
- **Safety status unit tests**: Tests verifying `safety_status`/`safety_provenance` populated on CDC001 (safe/unsafe), CDC002, CDC004, CDC005, CDC007, CDC008 findings.
- **Sync chain adversarial tests**: Missing middle stage, mismatched reset polarity detection.
- **CDC006 adversarial tests**: Bypass path detection, combinational between stage2-stage3.
- **SafetyStatus enum**: New enum in Finding model (Unknown, Candidate, VerifiedSafe, VerifiedUnsafe, Ambiguous).
- **Synchronizer warnings**: `SynchronizerChain` now carries `warnings` vector for reset polarity mismatches and fanout violations.
- **Stage reset validation**: `validate_stage_reset()` detects async reset on metastability-sensitive stages.
- **Stage fanout validation**: `validate_stage_fanout()` detects stage1 feeding both stage2 and unrelated logic.
- **JSON report envelope**: JSON output wrapped in `{"analysis_status":"...","finding_count":N,"findings":[...]}`.
- **JSON report new fields**: `is_gray_coded`, `has_handshake`, `source_module_path`, `dest_module_path`, `crosses_module_boundary` added to each finding.
- **HTML safety-status filter**: New dropdown to filter findings by safety status (Verified Safe/Unsafe/Candidate/Ambiguous).
- **Text report status indicator**: Text output now shows `Analysis status: <status>` header.
- **Report JSON safety field tests**: Tests verifying `safety_status`, `safety_provenance`, `is_gray_coded`, `has_handshake`, `source_module_path`, `dest_module_path`, `crosses_module_boundary` in JSON output.
- **HTML safety-status filter rendering test**: Test verifying `<select id="safety-filter">` is rendered with all options.
- **Text report analysis_status test**: Test verifying `Analysis status:` header appears in text output.
- **CDC008/009 mutation tests**: Tests for multi-domain daisy chain and reset domain crossing detection.
- **Mixed reset test**: Test verifying CDC007 fires at `info` severity when only one register lacks reset.
- **Rule interaction table**: Expanded to cover all 10 rules with interaction descriptions in `docs/rule-semantics.md`.
- **Safety provenance documentation**: Full provenance model documented in `docs/architecture.md`.
- **Missing reset adversarial fixture**: `missing_reset_adversarial.sv` + full-pipeline test.

**Changed**
- **CDC001 sync-chain description**: Updated docs to reflect that CDC002/004/005/007 are NOT suppressed when sync chain detected.
- **CDC002 detection description**: Updated docs to match structural PatternRecognizer (not substring matching).
- **Architecture docs**: Updated to reflect ClockResolver integration, analysis_status, suppress_reset_crossings, and CDC009 in pipeline.
- **Text report**: Uses structured `key=value` summary format.
- **Rule interaction table**: Expanded to cover all 10 rules with interaction descriptions.
- **Safety provenance documentation**: Full provenance model documented in architecture.
- **JSON field semantics**: Documented that `is_gray_coded`/`has_handshake` are informational hints, not safety-classification evidence.

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
