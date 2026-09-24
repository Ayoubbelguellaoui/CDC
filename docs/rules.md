# Rules Reference

## CDC001 — Unsynchronized Crossing

| Property | Value |
|----------|-------|
| Severity | error (info + VerifiedSafe when synchronizer detected) |
| Description | Register drives register across clock domains without synchronization |

**Detection**: An edge in the IR graph connects two registers in different clock domains, and no 2FF/3FF synchronizer chain is detected on the destination side.

**Sync-chain behavior**: When a 2FF or 3FF synchronizer chain is detected at the destination, CDC001 is downgraded to `info` with `safety_status=verified_safe`. CDC002, CDC004, CDC005, and CDC007 remain independent — they fire based on their own detection logic regardless of sync-chain status.

**Why it matters**: Data sampled by a register in a different clock domain can be metastable or inconsistent, leading to functional failures.

**Config**:
```yaml
rules:
  CDC001:
    enabled: true
    severity: error
```

---

## CDC002 — Multi-bit Crossing

| Property | Value |
|----------|-------|
| Severity | error |
| Description | Multi-bit bus crosses domains without gray-code encoding or handshake protocol |

**Detection**: A CDC001 crossing where the source register has width > 1 bit, and semantic analysis does not detect gray-code encoding, handshake protocol, or async FIFO pointer patterns on the source/destination registers.

**Why it matters**: Multi-bit buses can arrive at the destination domain with partial updates (e.g., some bits old, some new), causing data corruption.

**Config**:
```yaml
rules:
  CDC002:
    enabled: true
    severity: error
```

---

## CDC003 — Reconvergence Hazard

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Multiple paths from same source reconverge in destination domain |

**Detection**: A multi-bit source register fans out to two or more destination registers in a different domain, and those paths reconverge at a common consumer register. Single-bit sources are also reported (marked non-hazardous); for single-bit sources the finding is suppressed only when a synchronizer chain is detected on **both** destination paths.

**Why it matters**: Different bits of the same bus can arrive at the reconvergence point at different times, causing transient incorrect values.

**Config**:
```yaml
rules:
  CDC003:
    enabled: true
    severity: warning
```

---

## CDC004 — Gated Clock Crossing

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Register clocked by gated clock crosses to another domain |

**Detection**: A CDC001 crossing where the source register's clock is detected as gated (AND-gated with an enable signal). The check also covers the destination register and intermediate registers on the crossing path.

**Why it matters**: Gated clocks can cause glitches at the clock edge, increasing metastability risk.

**Config**:
```yaml
rules:
  CDC004:
    enabled: true
    severity: warning
```

---

## CDC005 — Muxed Clock No Reset

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Register clocked by muxed clock without reset signal |

**Detection**: A CDC001 crossing where the source register's clock is detected as muxed (selected by a conditional expression) and the register has no reset signal. The check also covers the destination register and intermediate registers on the crossing path.

**Why it matters**: Muxed clocks can cause runt pulses, and without a reset, the register state is undefined after power-up.

**Config**:
```yaml
rules:
  CDC005:
    enabled: true
    severity: warning
```

---

## CDC006 — Combinational Between Sync

| Property | Value |
|----------|-------|
| Severity | error |
| Description | Combinational logic or direct register feed between synchronizer stages |

**Detection**: A 2FF/3FF synchronizer chain where a stage has a combinational-logic predecessor and the chain entry is fed by a cross-domain register. Same-domain combinational feeds are not flagged.

**Why it matters**: Combinational logic between synchronizer stages defeats the purpose of synchronization by creating additional timing paths.

**Config**:
```yaml
rules:
  CDC006:
    enabled: true
    severity: error
```

---

## CDC007 — Missing Reset

| Property | Value |
|----------|-------|
| Severity | info (warning when `reset_policy.require_cdc_register_reset: true` and neither side has reset) |
| Description | CDC register without reset signal |

**Detection**: A CDC001 crossing where the source **or** destination register has an empty reset signal (`info`). Escalated to `warning` only when the strict reset policy is enabled **and** neither side has reset; mixed-reset crossings stay `info` regardless of policy.

**Why it matters**: Without reset, registers start in an undefined state, which can cause functional failures or excessive power consumption.

**Config**:
```yaml
rules:
  CDC007:
    enabled: true
    severity: warning
```

---

## CDC008 — Multi-domain Daisy Chain

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Signal crosses 3+ clock domains in a daisy chain |

**Detection**: A path that traverses 3 or more different clock domains through sequential register-to-register crossings.

**Why it matters**: Each domain crossing adds metastability risk. A daisy chain compounds this risk and makes timing analysis complex.

**Config**:
```yaml
rules:
  CDC008:
    enabled: true
    severity: warning
```

---

## CDC009 — Reset Domain Crossing

| Property | Value |
|----------|-------|
| Severity | warning (error for async-to-async reset domain crossings) |
| Description | Register crosses between different asynchronous reset domains |

**Detection**: Source and destination registers are grouped by `reset_signal + polarity + sync/async`; crossings between differing groups are flagged (async→async is `error`, otherwise `warning`). Two-stage reset synchronizers suppress the finding. Honors `reset_policy.check_same_clock_reset_crossings` and the `suppress_reset_crossings` config.

---

## CDC010 — Path Traversal Truncated

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Analysis limits may have hidden additional crossings |

**Detection**: Emitted when path traversal hits internal caps (10k paths, depth 50, 500k nodes / 1M edges). Sets `analysis_status=incomplete` and `SignoffStatus::Incomplete`.

---

## CDC011 — Pulse Crossing

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Pulse synchronizer without a proper 2FF chain |

---

## CDC012 — Toggle Crossing

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Toggle synchronizer without a proper 2FF chain |

---

## CDC013 — Unknown Propagation

| Property | Value |
|----------|-------|
| Severity | warning |
| Description | Uncertain value propagated through an unsynchronized path |

---

## Disabling Rules

```bash
# Disable a specific rule
opencdc check design.sv --top top --disable-rule CDC001

# Override severity
opencdc check design.sv --top top --severity CDC003=error
```

## Config File

```yaml
rules:
  CDC001:
    enabled: false
  CDC003:
    severity: error
  CDC007:
    enabled: true
    severity: info
```
