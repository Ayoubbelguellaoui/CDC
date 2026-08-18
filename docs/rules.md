# Rules

OpenCDC checks are organized as rules with stable IDs, severities, and descriptions.

## Rule Reference

### CDC001 — Unsynchronized Crossing

| Field | Value |
|---|---|
| **ID** | CDC001 |
| **Name** | unsynchronized_crossing |
| **Severity** | error |
| **Description** | Register drives register across clock domains without synchronization |

**When it fires:**
A register in clock domain A has a direct data path (through combinational logic or direct connection) to a register in clock domain B, and no 2FF or 3FF synchronizer chain is detected on the destination side.

**How to fix:**
- Add a 2FF or 3FF synchronizer chain on the destination side
- Use a handshake protocol
- Use a gray-code FIFO for multi-bit transfers
- Waive if the crossing is known safe

**Example:**
```systemverilog
// CDC001: src_ff (clk_a) drives dst_ff (clk_b) directly
always_ff @(posedge clk_a) src_ff <= data_in;
always_ff @(posedge clk_b) dst_ff <= src_ff;  // No synchronizer!
```

---

### CDC002 — Multi-bit Crossing

| Field | Value |
|---|---|
| **ID** | CDC002 |
| **Name** | multi_bit_crossing |
| **Severity** | error |
| **Description** | Multi-bit bus crosses clock domains without gray-code or handshake |

**When it fires:**
A multi-bit register (width > 1) crosses clock domains. Multi-bit signals require special handling because individual bits may arrive at different times, causing data corruption.

**How to fix:**
- Use gray-code encoding for counter/bus transfers
- Use a handshake protocol with valid/ready
- Use an async FIFO
- Use a multi-cycle path constraint if timing is guaranteed

---

### CDC003 — Reconvergence Hazard

| Field | Value |
|---|---|
| **ID** | CDC003 |
| **Name** | reconvergence_hazard |
| **Severity** | warning |
| **Description** | Multiple paths from same source reconverge in destination domain |

**When it fires:**
A single source register fans out to multiple destination registers in a different clock domain, and those paths reconverge at a common downstream register without synchronization on all paths.

**Hazard:**
Different synchronization latencies on the paths can cause the reconverging logic to see stale data from one path and new data from another, leading to functional errors.

**Example:**
```systemverilog
// src fans out to dst1 and dst2, both reconverge at consumer
always_ff @(posedge clk_a) src <= data;
always_ff @(posedge clk_b) dst1 <= src;
always_ff @(posedge clk_b) dst2 <= src;
always_ff @(posedge clk_b) consumer <= dst1 & dst2;  // Reconvergence!
```

**How to fix:**
- Ensure all paths are synchronized (same sync chain depth)
- Use a single synchronization point before fanout
- Waive if single-bit and no timing hazard

## Configuration

Rules can be configured via CLI flags:

```bash
# Disable a rule
opencdc check design.sv --top top --disable-rule CDC001

# Override severity
opencdc check design.sv --top top --severity CDC003=error
```

## Extending Rules

To add a new rule:

1. Define the rule in `src/rules/rule.cpp` `RuleEngine::RuleEngine()`
2. Implement the check in `src/cdc/`
3. Add unit tests in `tests/unit/`
4. Add a fixture in `tests/fixtures/sv/`
5. Add E2E regression test in `tests/regression/frontend_test.cpp`
6. Document in this file
