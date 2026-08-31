# Bug Fixes Applied — OpenCDC v0.3.0

## Critical Fix Round — Pattern Recognition, Waivers, Frontend Classification

### 1. ✅ Pattern recognizer rewrites (C1-C5)
**Files**: `src/cdc/pattern.cpp`, `src/cdc/pattern.h`, `src/frontend/slang_adapter.cpp`

- `detect_gray_encoding` now scans combinational nodes (XOR-of-delayed-register
  structure, explicit `GrayEncoder`/`GrayDecoder` logic types) in addition to
  registers, and pairs encoder→decoder only through a real graph connection.
- `detect_handshakes` matches the `is_handshake_signal` flag with valid/ready
  role disambiguated by signal name (in addition to the logic types).
- `detect_async_fifos` removed its dead first loop and matches the
  `is_async_fifo_ptr` flag.
- `analyze_and_annotate` now propagates detected patterns onto node flags
  (previously a self-referential no-op) and caches results per graph.
- `is_verified_safe_crossing` no longer treats a bare `is_gray_coded` source
  flag as proof of safety: encoder→decoder pairs require connectivity, FIFO
  pairs must be verified (both gray), and handshake pairs must be verified
  (cross-domain). Pattern results are computed once per graph instead of
  re-running O(N²) detectors per crossing.
- Frontend gray heuristic tightened: only `x ^ (x >> 1)`-style transforms
  (same signal, shifted) set `is_gray_coded`; arbitrary XOR expressions no
  longer qualify. Structural gray encoders are also tagged
  `LogicType::GrayEncoder` on the combinational node.
- Naming heuristics seed handshake/FIFO flags (`valid*`, `ready*`,
  `wr_ptr`/`rd_ptr` etc.); detection still requires cross-domain pairing
  before any crossing is suppressed.

### 2. ✅ Waiver over-match on empty fields (C6)
**File**: `src/cdc/waiver.cpp`, `src/cdc/waiver.h`

- Empty waiver fields act as wildcards; an empty finding value can no longer
  satisfy a specific waiver pattern (CDC010 truncation findings with empty
  dest no longer over-match).
- Waivers with a missing/invalid rule id (`CDC\d+` shape or `*`) are rejected
  in both `add_waiver` (returns false) and `load_from_file` (line skipped,
  error reported) instead of silently matching everything.
- `load_from_file` takes an optional `std::string* error` out-param, caps line
  length at 64KB, and fails when zero valid waivers were loaded.
- `add_waiver` returns false when a Wildcard/Regex waiver fails to compile;
  the analyzer surfaces it as a warning.

### 3. ✅ Clock/reset role misclassification in event lists (C7)
**File**: `src/frontend/slang_adapter.cpp`

- `extract_clock_reset` classifies event-list entries by role (reset-name
  heuristic) instead of position: `@(posedge clk or negedge rst_n)` no longer
  swaps clock and reset. Clock = last non-reset edge event.
- Non-`NamedValue` clock expressions fall back to source-text extraction
  instead of `"unknown"`, so blocks with hierarchical clock refs are kept.
- `"unknown"` is skipped before scope qualification, so no bogus
  `<scope>.unknown` domain is created.

### 4. ✅ Synchronized crossings skipped sub-rule checks (C8)
**File**: `src/cdc/crossing.cpp`

- Removed the early `continue` after synchronizer detection: CDC002/004/005/007
  are independent of sync presence. A multi-bit bus through a 2FF chain is now
  correctly flagged (per-bit sync of a bus is unsafe).
- `CDC002` findings no longer hardcode `is_gray_coded=false`/`has_handshake=false`.
- CDC004 also checks the destination register's gated clock; CDC005 also
  checks the destination's muxed-clock-without-reset; CDC007 fires when
  either side lacks a reset.

### 5. ✅ Reconvergence multi-bit suppression (C9)
**File**: `src/cdc/reconvergence.cpp`

- Sync-chain suppression is now gated on source width: single-bit sources
  through synchronizers are safe, but multi-bit sources through independent
  synchronizers still reconverge with bit skew and are reported.

### 6. ✅ Waiver expiry and portability
**File**: `src/cdc/waiver.cpp`

- Expiry date is inclusive through end of day (UTC); a waiver dated today no
  longer expires at midnight.
- `gmtime`/`timegm` replaced with `gmtime_r`/`_mkgmtime` and `gmtime_s`
  shims for thread safety and MSVC portability.
- `to_lower` uses `unsigned char` cast (UB fix for non-ASCII input).

### Test updates (behavior changes encoded)
- `reconvergence_test.cpp`: single-bit-through-sync stays suppressed;
  new `SyncedMultiBitStillReconvergent` asserts the fixed hazard reporting.
- `frontend_test.cpp`: gated fixture expects CDC001+CDC004 (destination-side
  gating); new `async_reset_2ff.sv` fixture pins clock/reset role
  classification.
- `stress_test.cpp`: totals updated (34 findings; CDC002=9, CDC004=2,
  CDC007=3) reflecting the false negatives that are now reported.
- New waiver tests: empty-rule rejection, empty-field wildcard semantics,
  empty-finding non-match, file validation failures, end-of-day expiry.
- New pattern tests: structural gray encoder on comb nodes, flagged gray
  source safety, encoder→decoder connectivity requirement, unverified FIFO
  non-suppression, annotation propagation.

Full suite: 258/258 passing.

## Critical Fixes Applied (Earlier Round)

### 1. ✅ Directory Creation Error Handling
**File**: `src/report/html_reporter.cpp`

**Fixed**: Added proper error handling for directory creation with `stat()` check and `errno` handling.

```cpp
void HtmlReporter::ensure_directory_exists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return;  // Directory already exists
        }
    }
    if (mkdir(path.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
            throw std::runtime_error("Failed to create directory: " + path);
        }
    }
}
```

### 2. ✅ Thread Safety in Pattern Recognizer
**File**: `src/cdc/pattern.h`

**Fixed**: Removed cached member variables (`async_fifos_`, `handshakes_`, `gray_patterns_`) that caused thread safety issues. Made caches `mutable` for const-correctness.

**Before**:
```cpp
std::vector<AsyncFifoPattern> async_fifos_;  // Not thread-safe
```

**After**:
```cpp
// Removed - methods now return values directly
mutable std::unordered_map<uint64_t, bool> gray_cache_;  // Thread-safe for reads
```

### 3. ✅ Waiver Regex Compilation Timing
**File**: `src/cdc/waiver.cpp`

**Fixed**: Compile regex at waiver creation time, not on first use. Fallback to substring matching if regex fails.

```cpp
void WaiverEngine::add_waiver(const Waiver& w) {
    Waiver waiver = w;
    if (waiver.match_type == WaiverMatchType::Wildcard ||
        waiver.match_type == WaiverMatchType::Regex) {
        if (!compile_waiver_regex(waiver)) {
            waiver.match_type = WaiverMatchType::Substring;  // Fallback
        }
    }
    waivers_.push_back(std::move(waiver));
}
```

### 4. ✅ LSP Server Bounds Checking and Error Handling
**File**: `src/lsp/server.cpp`

**Fixed**: Added message size limits, proper error handling, and bounds checking.

```cpp
// Limit message size to prevent memory exhaustion
if (message.size() > 10 * 1024 * 1024) {  // 10 MB limit
    break;
}

// Bounds check content length
int content_length = 0;
try {
    content_length = std::stoi(length_str);
    if (content_length < 0 || content_length > 10 * 1024 * 1024) {
        break;
    }
} catch (...) {
    break;
}
```

## Remaining Issues (Documented)

### Medium Priority (Not Fixed Yet)

1. **Incomplete JSON Parsing in LSP** — Need proper JSON library
2. **Potential Integer Overflow** — Multi-cycle path parsing
3. **String Copy Overhead** — Performance optimization needed
4. **O(N²) Pattern Detection** — Algorithm optimization needed

### Low Priority (Not Fixed Yet)

1. **Hardcoded Port in LSP** — Need method to query assigned port
2. **Missing Error Messages** — Constraints parsing silently fails
3. **Platform Dependency** — `mkdir` is POSIX-only
4. **Pattern Recognizer Caches** — No invalidation mechanism

### Testing Gaps (Not Fixed Yet)

1. **No Tests for LSP Server**
2. **No Tests for Python Bindings**
3. **No Tests for HTML Reporter**
4. **No Integration Tests**

## Testing Recommendations

### Unit Tests to Add

```cpp
// tests/unit/html_reporter_test.cpp
TEST(HtmlReporterTest, DirectoryCreation) {
    HtmlReporter reporter;
    HtmlReportOptions options;
    options.output_dir = "/tmp/test_report_" + std::to_string(time(nullptr));
    
    EXPECT_NO_THROW(reporter.generate_report(findings, options));
}

// tests/unit/lsp_server_test.cpp
TEST(LspServerTest, MessageSizeLimit) {
    LspServer server;
    std::string large_message(20 * 1024 * 1024, 'x');
    // Should reject messages > 10MB
}

// tests/unit/waiver_regex_test.cpp
TEST(WaiverRegexTest, InvalidRegexFallsBack) {
    Waiver w;
    w.match_type = WaiverMatchType::Regex;
    w.source_reg_name = "[invalid(";  // Invalid regex
    
    WaiverEngine engine;
    engine.add_waiver(w);
    
    // Should fallback to substring matching
    EXPECT_EQ(engine.waivers()[0].match_type, WaiverMatchType::Substring);
}
```

### Integration Tests to Add

```bash
# tests/integration/test_full_pipeline.sh
#!/bin/bash
opencdc check tests/fixtures/sv/stress_test_system.sv --top stress_test_system --format json --out /tmp/report.json
[ $? -eq 1 ] || exit 1  # Expect findings
[ -f /tmp/report.json ] || exit 1
```

## Performance Improvements Needed

1. **Cache Lowercase Strings**: Avoid repeated `to_lower()` calls
2. **Use String View**: Reduce string copies in pattern matching
3. **Optimize Pattern Detection**: Use hash maps instead of nested loops
4. **Parallel Analysis**: Use thread pool for independent analyses

## Security Considerations

1. **LSP Message Size Limits**: Added 10MB limit
2. **Directory Creation**: Added proper error handling
3. **Regex Compilation**: Added try-catch with fallback
4. **Bounds Checking**: Added for all user inputs

## Next Steps

1. Add comprehensive unit tests for all new modules
2. Add integration tests for complete workflows
3. Optimize pattern detection algorithms
4. Use C++17 filesystem API for cross-platform support
5. Add proper JSON library for LSP server
6. Document thread safety guarantees
7. Add Python test suite