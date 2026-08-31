**Changelog**
All notable changes to OpenCDC will be documented in this file.

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
