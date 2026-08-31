# Bug Report — OpenCDC v0.3.0 Implementation Review

## Critical Issues

### 1. **Memory Leak in LSP Server** (HIGH)
**File**: `src/lsp/server.cpp:225-226`

**Problem**: Client socket file descriptor is closed but not properly managed in all error paths.

```cpp
close(client_fd);  // Only closed in success path
```

**Impact**: If an exception occurs during message processing, the socket leaks.

**Fix**: Use RAII wrapper or ensure cleanup in all paths.

### 2. **Thread Safety Issue in Pattern Recognizer** (HIGH)
**File**: `src/cdc/pattern.cpp:176, 238`

**Problem**: `async_fifos_`, `handshakes_`, and `gray_patterns_` are member variables modified without synchronization.

```cpp
async_fifos_ = fifos;  // Not thread-safe
handshakes_ = patterns;  // Not thread-safe
```

**Impact**: Data races if multiple threads call detection methods concurrently.

**Fix**: Use mutex or return values instead of caching.

### 3. **Regex Error Handling Insufficient** (MEDIUM)
**File**: `src/cdc/waiver.cpp:90-94`

**Problem**: Invalid regex patterns silently fall back to string comparison.

```cpp
catch (const std::regex_error&) {
    return to_lower(pattern) == to_lower(value);  // Silent fallback
}
```

**Impact**: Users won't know their regex is invalid.

**Fix**: Log warning or throw exception for invalid regex.

### 4. **Directory Creation Race Condition** (MEDIUM)
**File**: `src/report/html_reporter.cpp:10`

**Problem**: `mkdir` doesn't check if directory already exists or handle errors.

```cpp
void HtmlReporter::ensure_directory_exists(const std::string& path) {
    mkdir(path.c_str(), 0755);  // No error checking
}
```

**Impact**: Fails silently if directory exists or permissions denied.

**Fix**: Check return value and errno, handle EEXIST gracefully.

## Moderate Issues

### 5. **Incomplete JSON Parsing in LSP** (MEDIUM)
**File**: `src/lsp/server.cpp:278-295`

**Problem**: Simple string parsing doesn't handle nested JSON or escaped characters.

```cpp
std::string LspServer::parse_string_field(const std::string& json, const std::string& field) {
    // Very basic parsing, doesn't handle escapes
}
```

**Impact**: Fails on complex documents with special characters.

**Fix**: Use proper JSON library (e.g., nlohmann/json).

### 6. **Potential Integer Overflow** (LOW)
**File**: `src/clock/constraints.cpp:208`

**Problem**: `std::stoi` can throw on large numbers, but caught silently.

```cpp
try {
    mcp.cycles = std::stoi(tok);
} catch (...) {}  // Silently ignored
```

**Impact**: Multi-cycle paths with invalid values silently ignored.

**Fix**: Log warning for parse failures.

### 7. **Missing Null Checks** (LOW)
**File**: `src/cdc/pattern.cpp:157-158`

**Problem**: `find_node` can return nullptr but not always checked.

```cpp
const ir::Node* rd = graph.find_node(rd_id);
const ir::Node* wr = graph.find_node(wr_id);
if (rd && wr && rd->clock_domain != wr->clock_domain) {  // Good
```

But elsewhere:
```cpp
const ir::Node* valid_node = graph.find_node(valid_id);
const ir::Node* ready_node = graph.find_node(ready_id);
if (!valid_node || !ready_node) return false;  // Good
```

**Status**: Actually properly handled in most places.

### 8. **String Copy Overhead** (LOW)
**File**: `src/cdc/pattern.cpp:33, 49, 75, 113, 142, 189`

**Problem**: `std::transform` creates temporary strings repeatedly.

```cpp
std::string lower_name = node.hier_name;
std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
```

**Impact**: Performance overhead on large designs.

**Fix**: Cache lowercased names or use string_view with case-insensitive comparison.

## Minor Issues

### 9. **Hardcoded Port in LSP** (LOW)
**File**: `src/lsp/server.cpp:25`

**Problem**: Default port is hardcoded to 0 (auto-assign), but no way to query assigned port.

**Fix**: Add method to get actual port after start().

### 10. **Missing Error Messages** (LOW)
**File**: `src/clock/constraints.cpp:196`

**Problem**: SDC parsing errors are silently ignored.

```cpp
if (!file.is_open()) return ClockConstraints{};  // No error message
```

**Fix**: Add error logging or return error status.

### 11. **Inconsistent Return Types** (LOW)
**File**: `src/cdc/pattern.cpp`

**Problem**: Some detection methods return vectors, others modify member variables.

```cpp
std::vector<AsyncFifoPattern> detect_async_fifos(...);  // Returns vector
void analyze_and_annotate(...);  // Modifies graph
```

**Fix**: Consistent API - either all return or all modify.

### 12. **No Bounds Checking** (LOW)
**File**: `src/lsp/server.cpp:208`

**Problem**: `std::stoi` without bounds checking.

```cpp
int content_length = std::stoi(length_str);  // Could overflow
```

**Impact**: Malicious LSP client could cause overflow.

**Fix**: Validate content_length before use.

## Design Issues

### 13. **Waiver Regex Compilation on Every Match** (MEDIUM)
**File**: `src/cdc/waiver.cpp:105-130`

**Problem**: Regex is compiled on first use with `const_cast` hack.

```cpp
bool WaiverEngine::compile_waiver_regex(Waiver& w) {
    if (w.regex_compiled) return true;
    // ... compile
}
```

**Impact**: Thread-unsafe, performance overhead.

**Fix**: Compile at waiver creation time, not on first use.

### 14. **Pattern Recognizer Caches Without Invalidation** (LOW)
**File**: `src/cdc/pattern.cpp:176, 238`

**Problem**: Caches are never invalidated if graph changes.

```cpp
async_fifos_ = fifos;  // Cached but never cleared
```

**Impact**: Stale data if graph is modified after analysis.

**Fix**: Add clear_cache() method or use weak references.

### 15. **HTML Reporter Platform Dependency** (LOW)
**File**: `src/report/html_reporter.cpp:10`

**Problem**: Uses POSIX `mkdir` which won't work on Windows.

```cpp
#include <sys/stat.h>  // POSIX only
mkdir(path.c_str(), 0755);
```

**Fix**: Use `std::filesystem::create_directory` (C++17).

## Testing Gaps

### 16. **No Tests for LSP Server**
**File**: `tests/unit/` (missing)

**Problem**: No unit tests for LSP server functionality.

**Fix**: Add `lsp_server_test.cpp`.

### 17. **No Tests for Python Bindings**
**File**: `tests/python/` (missing)

**Problem**: Python bindings not tested.

**Fix**: Add Python test suite.

### 18. **No Tests for HTML Reporter**
**File**: `tests/unit/` (missing)

**Problem**: HTML report generation not tested.

**Fix**: Add `html_reporter_test.cpp`.

### 19. **No Integration Tests**
**File**: `tests/integration/` (missing)

**Problem**: No end-to-end tests for complete workflows.

**Fix**: Add integration tests for:
- Full analysis pipeline
- LSP server communication
- Python bindings
- HTML report generation

## Performance Issues

### 20. **O(N²) Pattern Detection** (MEDIUM)
**File**: `src/cdc/pattern.cpp:155-172`

**Problem**: Nested loops over all nodes for async FIFO detection.

```cpp
for (uint64_t rd_id : read_it->second) {
    for (uint64_t wr_id : write_it->second) {  // O(N²)
```

**Impact**: Slow on large designs with many FIFOs.

**Fix**: Use hash maps or limit search scope.

### 21. **Repeated String Allocations** (LOW)
**File**: Multiple files

**Problem**: `to_lower()` creates new strings repeatedly.

```cpp
static std::string to_lower(const std::string& s) {
    std::string r = s;  // Copy
    std::transform(...);
    return r;  // Another copy
}
```

**Fix**: Use case-insensitive comparison or string_view.

## Documentation Issues

### 22. **Missing Error Handling Documentation**
**Problem**: No documentation on error codes and recovery strategies.

**Fix**: Add error handling section to documentation.

### 23. **No Thread Safety Documentation**
**Problem**: No documentation on which classes are thread-safe.

**Fix**: Add thread safety guarantees to API documentation.

### 24. **Incomplete Python API Docs**
**Problem**: Python bindings documented but no examples for advanced usage.

**Fix**: Add comprehensive Python examples.

## Recommendations

### Priority 1 (Fix Before Release)
1. Fix thread safety in PatternRecognizer
2. Add proper error handling in LSP server
3. Fix directory creation in HTML reporter
4. Add regex validation error messages

### Priority 2 (Fix Soon)
5. Use proper JSON parser in LSP server
6. Add bounds checking for LSP messages
7. Compile regex waivers at creation time
8. Add missing unit tests

### Priority 3 (Future Improvements)
9. Use C++17 filesystem API
10. Optimize pattern detection algorithms
11. Add integration tests
12. Improve documentation