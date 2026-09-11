# What OpenCDC Can Prove

This document describes what OpenCDC can prove, what it cannot prove, and
what remains uncertain without additional user annotation.

## What OpenCDC Proves With Certainty

### Structural Facts
- **Register identification**: A node is a register (storage element) in the
  design. This is derived from the SystemVerilog AST via slang.
- **Clock domain membership**: Registers are grouped into clock domains based
  on their clock signal (root_clock or clock_domain field). Two registers are
  in the same domain if they share the same root clock.
- **Reset signal association**: Each register has a reset signal name, polarity
  (active-high/active-low), and async/sync flag. These are extracted from the
  SystemVerilog AST.
- **Bus width**: The width (in bits) of each register.
- **Module hierarchy**: The module path of each register within the design.

### Cross-Domain Detection (CDC001)
A crossing is detected when a register in one clock domain drives a register
in a different clock domain. This is a **structural fact** — no inference
or guessing is involved. The detection is:
- **Complete** for register-to-register paths within the analyzed scope.
- **Exact** on the clock domain boundary — false negatives are impossible
  when the frontend correctly extracts clock domains.

### Synchronizer Detection (CDC001 with synchronizer)
OpenCDC detects 2FF, 3FF, 4FF, and N-stage synchronizer chains at the
destination register of a crossing. Detection is based on:
- Chain depth (2, 3, 4, or 5+ stages)
- Width == 1 (single-bit only)
- All stages in the same clock domain
- No combinational logic between stages
- Strict mode: no same-domain register predecessors on intermediate stages

**What this proves**: A synchronizer chain exists on the destination side.
**What this does not prove**: That the synchronizer is correctly placed
(reset synchronizer vs. data synchronizer), that it meets timing
constraints, or that it handles all metastability scenarios.

### Pattern Verification
- **Gray code**: Verifies that a register with `is_gray_coded` flag has
  only single-bit transitions. Without the flag, detects but does not verify.
- **Async FIFO**: Verifies gray-encoded pointers AND synchronized read/write
  pointers across domains.
- **Handshake**: Verifies cross-domain path, data path, AND feedback path
  (valid drives register in ready domain, ready drives register back).

### Reset Domain Detection (CDC009)
- Registers with different reset signals or polarities are in different
  reset domains.
- Async reset crossings are escalated to "error" severity.
- Reset synchronizer detection: if a 2FF+ chain exists in the destination
  domain using the destination reset signal, the crossing is suppressed.

### Clock Relationship Classification
OpenCDC classifies the relationship between any two clock domains:
- **Same**: Same clock signal or domain ID.
- **Asynchronous**: Declared via `set_clock_groups -asynchronous`.
- **Exclusive**: Declared via `set_clock_groups -exclusive`.
- **Generated**: One clock is `create_generated_clock` from the other.
- **Related**: Share common master clock (both generated from same source).
- **Gated**: One clock is gated (AND logic) version of the other.
- **Muxed**: One clock is muxed (MUX logic) version of the other.
- **Synchronous**: Integer frequency ratio (both have known frequencies).
- **Unknown**: No relationship evidence from constraints or resolver.

## What OpenCDC Infers (High Confidence)

### Safety Status
- **VerifiedSafe**: A synchronizer chain is detected and has no structural
  warnings. CDC001 is reported at `info` (audit trail), not as a warning.
  This is high confidence but not a timing guarantee.
- **VerifiedUnsafe**: No synchronizer detected, or structural issue found
  (missing reset, gated clock, muxed clock without reset).
- **Ambiguous**: Suppressed by constraint (false path, multicycle), or
  path traversal truncated.
- **Candidate**: Advisory findings (missing reset on one side only,
  daisy chain, same-clock reset crossing).

### Severity Reduction
- **Synchronous/related clocks**: CDC001 reduced to "warning" since
  synchronizer may not be needed for frequency-locked clocks.
- **Exclusive clocks**: CDC001 recorded as "info" audit trail.

## What OpenCDC Cannot Prove

### Timing and Metastability
- **Setup/hold timing**: OpenCDC does not perform static timing analysis.
  A synchronizer that exists structurally may still fail timing.
- **Metastability resolution time**: OpenCDC does not verify that the
  synchronizer chain is long enough for the target MTBF.
- **Clock skew**: OpenCDC does not model clock skew between domains.

### Functional Correctness
- **Data integrity**: OpenCDC does not verify that data is correctly
  transferred across domains (only that synchronization infrastructure
  exists).
- **Protocol compliance**: OpenCDC does not verify that handshake or
  FIFO protocols are correctly implemented beyond structural checks.
- **Reset behavior**: OpenCDC does not model reset assertion/deassertion
  timing or metastability during reset.

### Coverage
- **Unreachable paths**: OpenCDC may miss crossings through very deep
  combinational logic or through IP blocks not analyzed.
- **Glitch paths**: OpenCDC does not detect glitches on combinational
  paths between registers.
- **Latch-based designs**: Latches are not modeled as registers.

### What Requires User Annotation
- **Clock relationships** without constraints: When no SDC/YAML constraints
  file is provided, clock relationships are classified as "Unknown".
- **Exclusive clock groups**: Must be declared via `set_clock_groups
  -exclusive` or config YAML.
- **False paths**: Must be declared via `set_false_path` or config YAML.
- **Multicycle paths**: Must be declared via `set_multicycle_path` or
  config YAML.
- **Reset methodology**: The tool detects reset domain crossings but
  cannot determine if the methodology is intentional without user
  configuration.

## Known Limitations

1. **Single-bit synchronizer only**: Multi-bit buses require gray coding,
   handshake, or async FIFO — not standard 2FF synchronizers.
2. **Depth limit**: Synchronizer chains longer than 10 stages are truncated.
   The tool reports NStage for 5+ stages but does not walk beyond 10.
3. **Name-based reset grouping**: Reset domains are grouped by signal name,
   not by structural reset tree analysis.
4. **No hierarchical flattening**: IP blocks and black boxes are not
   analyzed internally.
5. **No formal verification**: OpenCDC is a static analysis tool, not a
   model checker. It cannot prove absence of metastability or timing
   violations.
