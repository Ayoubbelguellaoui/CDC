# OpenCDC Rule Semantics

This document defines the intent, violation conditions, required evidence, safe conditions, exemptions, constraint interactions, and false-positive/negative risks for each CDC rule.

## CDC001 — Unsynchronized Crossing

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect register-to-register paths crossing clock domains without a synchronizer chain. |
| **Violation** | A register in domain A drives a register in domain B with no 2FF/3FF synchronizer chain on the destination side. |
| **Required Evidence** | Cross-domain register path (verified by `CrossingAnalyzer`), no `SyncPattern::TwoFF` or `SyncPattern::ThreeFF` detected at destination. |
| **Safe Conditions** | Synchronizer chain present (downgraded to warning), false-path constraint, gray-code/handshake/FIFO crossing, waived. |
| **Exemptions** | None — always emitted (but severity varies). |
| **Constraint Interaction** | `false_paths` suppresses. `multi_cycle_path` does not suppress. |
| **False-Positive Risk** | Low. Only fires on verified cross-domain register paths. |
| **False-Negative Risk** | Medium. Paths through combinational logic or gated clocks may be missed if not fully traced. |

## CDC002 — Multi-Bit Crossing

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect multi-bit buses crossing clock domains without gray-code encoding, handshake protocol, or async FIFO pattern. |
| **Violation** | Source register width > 1, crossing to different domain, and `is_verified_safe_crossing` returns false. |
| **Required Evidence** | Cross-domain path, width > 1, no structural gray encoder→decoder, handshake valid/ready pair, or async FIFO pointer pair. |
| **Safe Conditions** | Gray-code verified, handshake protocol verified, async FIFO pointer verified. |
| **Exemptions** | None. |
| **Constraint Interaction** | False-path suppresses (no crossing reported at all). |
| **False-Positive Risk** | Medium. Name-based heuristic flags (`is_gray_coded`, `is_handshake_signal`) do NOT bypass — only structural verification matters. |
| **False-Negative Risk** | Low. Structural verification is conservative. |

## CDC003 — Reconvergence Hazard

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect single-source fanout through independent synchronizer paths that reconverge. |
| **Violation** | Register fans out to ≥2 cross-domain destinations, paths reconverge at a downstream register. |
| **Required Evidence** | Fanout source with ≥2 cross-domain successors, BFS-traced reconvergence within bounds. |
| **Safe Conditions** | Source width ≤ 1 AND destination has sync chain (TwoFF or ThreeFF). |
| **Exemptions** | Single-bit sources with sync chains on both paths. |
| **Constraint Interaction** | False-path suppresses individual crossings; reconvergence may still be detected on non-false-path paths. |
| **False-Positive Risk** | Medium. BFS truncation (16 nodes, 3 hops) may miss reconvergence beyond bounds. |
| **False-Negative Risk** | Medium. Truncation limits detection depth. |

## CDC004 — Gated Clock Crossing

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect register clocked by a gated clock on a cross-domain path. |
| **Violation** | Source or destination register has `clock_is_gated == true`. |
| **Required Evidence** | `clock_is_gated` flag set by frontend or clock resolver. |
| **Safe Conditions** | Neither register has gated clock. |
| **Exemptions** | None. |
| **Constraint Interaction** | False-path suppresses. |
| **False-Positive Risk** | Low. Depends on accurate gated-clock detection. |
| **False-Negative Risk** | Low. Gated-clock detection is conservative. |

## CDC005 — Muxed Clock Without Reset

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect register clocked by a muxed clock without a reset signal. |
| **Violation** | Source or destination has `clock_is_muxed == true` AND `reset_signal.empty()`. |
| **Required Evidence** | `clock_is_muxed` flag and empty reset signal. |
| **Safe Conditions** | Register has a reset signal, or clock is not muxed. |
| **Exemptions** | None. |
| **Constraint Interaction** | False-path suppresses. |
| **False-Positive Risk** | Low. |
| **False-Negative Risk** | Low. |

## CDC006 — Combinational Logic in Sync Chain

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect combinational logic between synchronizer stages or before the first sync stage. |
| **Violation** | Combinational node found between stages driven by cross-domain register, OR combinational node before first stage driven by cross-domain register. |
| **Required Evidence** | Combinational node in the path between sync stages, or between cross-domain source and first sync stage. |
| **Safe Conditions** | No combinational logic in the synchronizer chain path. |
| **Exemptions** | None. |
| **Constraint Interaction** | False-path suppresses. |
| **False-Positive Risk** | Low. Only fires when combinational logic is structurally verified between stages. |
| **False-Negative Risk** | Low. |

## CDC007 — Missing Reset

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect CDC crossing where both registers lack a reset signal. |
| **Violation** | Both source and destination registers have empty `reset_signal`. |
| **Required Evidence** | `reset_signal.empty()` on both registers. |
| **Safe Conditions** | At least one register has a reset signal. |
| **Exemptions** | None. |
| **Constraint Interaction** | False-path suppresses. |
| **False-Positive Risk** | Low. Only fires when both registers truly lack reset. |
| **False-Negative Risk** | Low. |

## CDC008 — Multi-Domain Daisy Chain

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect register chains crossing ≥3 clock domains. |
| **Violation** | Register chain visits ≥3 distinct clock domains. |
| **Required Evidence** | DFS traversal from source visiting ≥3 domain IDs. |
| **Safe Conditions** | Chain crosses fewer than 3 domains. |
| **Exemptions** | None. |
| **Constraint Interaction** | False-path suppresses individual domain crossings. |
| **False-Positive Risk** | Low. |
| **False-Negative Risk** | Low. |

## CDC009 — Reset Domain Crossing

| Attribute | Description |
|-----------|-------------|
| **Intent** | Detect crossing between registers in different reset domains. |
| **Violation** | Source and destination in different reset domains (different reset signals or polarities). |
| **Required Evidence** | Reset domain analysis by `ResetDomainAnalyzer`. |
| **Safe Conditions** | Same reset domain, or waived. |
| **Exemptions** | Suppressed by `suppress_reset_crossings` config. |
| **Constraint Interaction** | Config-driven suppression. |
| **False-Positive Risk** | Medium. Reset domain extraction depends on frontend accuracy. |
| **False-Negative Risk** | Medium. |

## CDC010 — Path Traversal Truncated

| Attribute | Description |
|-----------|-------------|
| **Intent** | Warn when path traversal exceeds safety limits. |
| **Violation** | Graph traversal hits `MAX_PATHS` (10000) or `MAX_NODES` (50000) limit. |
| **Required Evidence** | Traversal counter exceeded. |
| **Safe Conditions** | Full traversal completed without truncation. |
| **Exemptions** | None — informational warning. |
| **Constraint Interaction** | None. |
| **False-Positive Risk** | N/A — diagnostic, not a real finding. |
| **False-Negative Risk** | High. Truncation means some crossings may be missed. |

## Rule Interaction Matrix

Legend: `fires` = both rules fire on same crossing; `suppressed` = first rule suppressed by second; `downgraded` = severity reduced; `—` = no interaction.

| | CDC001 | CDC002 | CDC003 | CDC004 | CDC005 | CDC006 | CDC007 | CDC008 | CDC009 | CDC010 |
|---|---|---|---|---|---|---|---|---|---|---|
| **CDC001** | — | fires if width>1 | fires if reconvergent | fires | fires | fires | fires | fires | fires | may truncate |
| **CDC002** | fires if width>1 | — | fires if reconvergent | fires | fires | fires | fires | fires | fires | may truncate |
| **CDC003** | suppressed if sync+single-bit | independent | — | independent | independent | independent | independent | independent | independent | may truncate |
| **CDC004** | fires | fires | independent | — | independent | independent | fires | fires | fires | may truncate |
| **CDC005** | fires | fires | independent | independent | — | independent | fires | fires | fires | may truncate |
| **CDC006** | fires (independent) | independent | independent | independent | independent | — | independent | independent | independent | may truncate |
| **CDC007** | fires (independent) | independent | independent | fires | fires | independent | — | fires | fires | may truncate |
| **CDC008** | fires | fires | independent | fires | fires | independent | fires | — | fires | may truncate |
| **CDC009** | fires | fires | independent | fires | fires | independent | fires | fires | — | may truncate |
| **CDC010** | — | — | — | — | — | — | — | — | — | — |

### Key Interactions
- **CDC001 + CDC002**: Multi-bit unsynchronized crossing fires both (CDC001 error + CDC002 error).
- **CDC001 + CDC003**: Reconvergence suppresses CDC001 for single-bit sources with sync chains.
- **CDC001 + CDC006**: Combinational logic before first sync stage fires CDC006 independently of CDC001.
- **CDC004/CDC005 + CDC007**: Gated/muxed clock without reset fires both clock and reset rules.
- **CDC009**: Fires independently based on reset domain analysis, orthogonal to clock domain crossing.
- **CDC010**: Diagnostic only — indicates other rules may have incomplete coverage.

## JSON Report Field Semantics

The following fields in the JSON report are **informational hints only**, not safety-classification evidence:

| Field | Meaning | Safe to use for decisions? |
|-------|---------|---------------------------|
| `is_gray_coded` | Node-level gray-code flag (may include name-based hints) | No — use `safety_status` instead |
| `has_handshake` | Node-level handshake flag (may include name-based hints) | No — use `safety_status` instead |

The **authoritative safety classification** is:
- `safety_status`: `verified_safe`, `verified_unsafe`, `candidate`, `ambiguous`, or absent (unknown)
- `safety_provenance`: Human-readable explanation of the evidence chain

If `safety_status` is absent or `unknown`, the crossing has not been conclusively classified.
