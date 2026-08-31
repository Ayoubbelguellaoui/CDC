# OpenCDC v0.3.0 — Enhancement Summary

## Overview

This release addresses all critical weaknesses identified in the initial review and adds significant new capabilities for CDC analysis.

## Critical Fixes Implemented

### 1. Semantic Gray-Code Detection (Priority 1)

**Problem**: CDC002 relied on substring matching ("gray", "handshake") which produced false positives/negatives.

**Solution**: Implemented `PatternRecognizer` class with semantic analysis:
- Detects gray-code encoding patterns by analyzing XOR logic
- Recognizes async FIFO pointer patterns (rd_ptr_gray, wr_ptr_gray)
- Identifies handshake protocols (valid/ready signal pairs)
- Checks IR node `LogicType` flags (GrayEncoder, GrayDecoder, HandshakeValid, HandshakeReady)

**Files**:
- `src/cdc/pattern.h` — Pattern recognizer interface
- `src/cdc/pattern.cpp` — Implementation with semantic detection
- `src/ir/graph.h` — Added `LogicType` enum and pattern flags to Node

### 2. Async FIFO Recognition (Priority 1)

**Problem**: No recognition of async FIFO structures, leading to false CDC002 violations.

**Solution**: Added async FIFO pattern detection:
- Identifies gray-coded read/write pointer pairs
- Detects dual-clock FIFO modules by naming conventions
- Verifies proper pointer synchronization
- Automatically suppresses CDC002 for recognized async FIFOs

**Implementation**: `PatternRecognizer::detect_async_fifos()`

### 3. Enhanced SystemVerilog Support (Priority 2)

**Problem**: Limited SV support (only `always_ff` blocks).

**Solution**: Extended frontend to support:
- `always_comb` blocks
- Continuous assignments (`assign` statements)
- Improved muxed clock detection from ternary expressions
- Logic type extraction from binary operators (AND, OR, XOR)

**Files**:
- `src/frontend/slang_adapter.cpp` — Extended to handle `always_comb` and continuous assignments

### 4. Regex-Based Waivers (Priority 2)

**Problem**: Substring matching was too broad, matching unintended registers.

**Solution**: Implemented three waiver matching modes:
- **Substring** (default): Backward-compatible substring matching
- **Wildcard**: Supports `*` and `?` patterns (e.g., `top.*.src_*`)
- **Regex**: Full regex support for complex patterns

**Usage**:
```
# Substring (default)
CDC001 src dst clk_a clk_b "Safe" @team

# Wildcard
WILDCARD CDC001 top.*.src_* top.*.dst_* clk_a clk_b "Module waiver" @team

# Regex
REGEX CDC001 top\..*\.src_.* top\..*\.dst_.* clk_a clk_b "Regex waiver" @team
```

**Files**:
- `src/cdc/waiver.h` — Added `WaiverMatchType` enum and regex support
- `src/cdc/waiver.cpp` — Implemented wildcard-to-regex conversion and matching

### 5. Clock Constraints File (Priority 2)

**Problem**: No integration with STA tools or timing constraints.

**Solution**: Implemented SDC and YAML constraint file support:
- **SDC Reader**: Parses `create_clock`, `create_generated_clock`, `set_false_path`, `set_multicycle_path`, `set_clock_groups`
- **YAML Format**: Human-readable constraint format
- **Integration**: Automatically imports false paths from constraints

**Usage**:
```bash
opencdc check design.sv --top top --constraints constraints.sdc
```

**Files**:
- `src/clock/constraints.h` — Constraint structures and parsers
- `src/clock/constraints.cpp` — SDC and YAML parsing implementation

### 6. Combinational Logic Tracking (Priority 2)

**Problem**: CDC006 couldn't distinguish between direct register feed and combinational logic.

**Solution**: Added logic type tracking to IR:
- `LogicType` enum: None, And, Or, Xor, Not, Mux, Concat, GrayEncoder, GrayDecoder, HandshakeValid, HandshakeReady, AsyncFifoPtr
- Logic input tracking in Node structure
- Pattern recognizer annotates nodes with detected types

**Files**:
- `src/ir/graph.h` — Added `LogicType` and logic tracking fields

### 7. HTML Report Generator (Priority 3)

**Problem**: Limited reporting (JSON and text only).

**Solution**: Implemented interactive HTML reports:
- **Dashboard**: Summary cards with error/warning/waived counts
- **Charts**: Severity and rule distribution bar charts
- **Filtering**: Search, severity filter, rule filter
- **Dark Mode**: Automatic dark mode support
- **Responsive**: Mobile-friendly layout

**Usage**:
```bash
opencdc check design.sv --top top --format html --html-dir report
```

**Files**:
- `src/report/html_reporter.h` — HTML reporter interface
- `src/report/html_reporter.cpp` — HTML generation with CSS/JS

### 8. Reset Domain Analysis (Priority 3)

**Problem**: Only checked if reset exists, not reset domain crossings.

**Solution**: Implemented reset domain extraction and crossing detection:
- Groups registers by reset signal and polarity
- Detects CDC crossings between different reset domains
- Reports CDC007 for reset domain crossings

**Files**:
- `src/cdc/reset_domain.h` — Reset domain analyzer
- `src/cdc/reset_domain.cpp` — Implementation

### 9. Trend Analysis (Priority 3)

**Problem**: No way to track CDC findings across runs.

**Solution**: Implemented baseline comparison:
- Save baseline findings to file
- Compare current run against baseline
- Report new, fixed, and persistent findings
- Track rule-level deltas

**Usage**:
```bash
# Save baseline
opencdc check design.sv --top top --out baseline.json

# Compare against baseline
opencdc check design.sv --top top --baseline baseline.json
```

**Files**:
- `src/analysis/trend.h` — Trend analyzer interface
- `src/analysis/trend.cpp` — Baseline save/load and comparison

### 10. Performance Optimizations (Priority 3)

**Problem**: Single-threaded analysis on large designs.

**Solution**: Implemented parallel processing utilities:
- `parallel_for` — Parallel iteration over collections
- `parallel_map` — Parallel transform with results
- `ThreadPool` — Work-stealing thread pool
- `ThreadSafeQueue` — Lock-free queue for task distribution

**Files**:
- `src/util/parallel.h` — Parallel processing utilities

## Architecture Improvements

### IR Graph Enhancements

```cpp
enum class LogicType {
    None, And, Or, Xor, Not, Mux, Concat,
    GrayEncoder, GrayDecoder, HandshakeValid, 
    HandshakeReady, AsyncFifoPtr, Unknown
};

struct Node {
    // Existing fields...
    LogicType logic_type = LogicType::None;
    std::vector<uint64_t> logic_inputs;
    std::string logic_expression;
    bool is_gray_coded = false;
    bool is_handshake_signal = false;
    bool is_async_fifo_ptr = false;
};
```

### Pattern Recognizer

```cpp
class PatternRecognizer {
public:
    std::vector<AsyncFifoPattern> detect_async_fifos(const ir::Graph& graph);
    std::vector<HandshakePattern> detect_handshakes(const ir::Graph& graph);
    std::vector<GrayCodePattern> detect_gray_encoding(const ir::Graph& graph);
    
    bool is_gray_coded(uint64_t node_id, const ir::Graph& graph) const;
    bool is_handshake_signal(uint64_t node_id, const ir::Graph& graph) const;
    bool is_async_fifo_ptr(uint64_t node_id, const ir::Graph& graph) const;
    
    void analyze_and_annotate(ir::Graph& graph);
};
```

### Clock Constraints

```cpp
struct ClockConstraints {
    std::vector<ClockDefinition> clocks;
    std::vector<FalsePath> false_paths;
    std::vector<MultiCyclePath> multi_cycle_paths;
    std::vector<ClockGroup> clock_groups;
    
    bool is_false_path(const std::string& from, const std::string& to) const;
    bool is_asynchronous(const std::string& clk1, const std::string& clk2) const;
};

class SdcReader {
public:
    ClockConstraints read_sdc(const std::string& path);
    ClockConstraints parse_sdc_content(const std::string& content);
};
```

## New CLI Options

```bash
--constraints <file>    Clock constraints file (SDC or YAML)
--format html           Generate HTML report
--html-dir <dir>        HTML output directory (default: opencdc_report)
```

## File Structure

```
src/
├── analysis/
│   ├── trend.h
│   └── trend.cpp
├── cdc/
│   ├── pattern.h
│   ├── pattern.cpp
│   ├── reset_domain.h
│   └── reset_domain.cpp
├── clock/
│   ├── constraints.h
│   └── constraints.cpp
├── report/
│   ├── html_reporter.h
│   └── html_reporter.cpp
├── util/
│   └── parallel.h
└── ir/
    └── graph.h (enhanced)
```

## Testing Recommendations

1. **Pattern Recognition**: Test with gray-coded counters, async FIFOs, handshake protocols
2. **Waiver Matching**: Test wildcard and regex patterns against various register names
3. **Constraints**: Test SDC parsing with real constraint files
4. **HTML Reports**: Generate reports and verify all features work
5. **Reset Domains**: Test designs with multiple reset signals
6. **Trend Analysis**: Compare multiple runs and verify delta calculations

## Performance Impact

- **Pattern Recognition**: O(N) where N is number of nodes
- **Parallel Processing**: Scales with number of CPU cores
- **Memory**: Minimal overhead from pattern caching
- **HTML Generation**: O(F) where F is number of findings

## Backward Compatibility

All changes are backward compatible:
- Default waiver mode is substring (existing behavior)
- New CLI options are optional
- IR graph additions are non-breaking
- Pattern recognizer is optional (can be disabled)

## Future Enhancements

Remaining tasks for future releases:
1. Python bindings for scripting
2. LSP server for IDE integration
3. Generate block support
4. Interface and modport support
5. Multi-cycle path analysis
6. Incremental analysis mode