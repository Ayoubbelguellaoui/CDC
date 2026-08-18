# Waivers

Waivers allow you to suppress known-safe CDC findings while maintaining an auditable record.

## Format

Waiver files use a simple line-based format:

```
# Comments start with #
RULE_ID  SOURCE_REG  DEST_REG  SRC_DOMAIN  DST_DOMAIN  "JUSTIFICATION"  @OWNER  EXPIRY_DATE
```

### Fields

| Field | Required | Description |
|---|---|---|
| RULE_ID | Yes | Rule to waive (e.g., CDC001) |
| SOURCE_REG | Yes | Source register hierarchical name |
| DEST_REG | Yes | Destination register hierarchical name |
| SRC_DOMAIN | Yes | Source clock domain |
| DST_DOMAIN | Yes | Destination clock domain |
| JUSTIFICATION | Yes | Quoted string explaining why the waiver is safe |
| OWNER | No | `@username` of person who approved the waiver |
| EXPIRY_DATE | No | `YYYY-MM-DD` — waiver expires after this date |

### Matching Rules

- Matching is **case-insensitive**
- Empty fields match any value (wildcard)
- Expired waivers are automatically skipped
- First matching waiver wins

## Example

```
# Waivers for verified crossings
CDC001 mod.src_ff mod.dst_ff clk_a clk_b "Reviewed by designer, 2FF sync added downstream" @john 2026-12-31
CDC003 mod.data mod.meta1 mod.meta2 clk_a clk_b "Single-bit, no timing hazard" @jane
```

## Usage

```bash
opencdc check design.sv --top top --waiver waivers.txt
```

## Behavior

- Waived findings are **still included** in the report with `"waived": true`
- Waived errors do **not** cause exit code 1
- Waiver justification and owner appear in both JSON and text output
- Expired waivers are treated as if they don't exist

## JSON Output for Waived Findings

```json
{
  "rule_id": "CDC001",
  "severity": "error",
  "waived": true,
  "waiver_justification": "Reviewed by designer, 2FF sync added downstream",
  "waiver_owner": "john",
  "source": "mod.src_ff",
  "dest": "mod.dst_ff"
}
```

## Auditing

Waivers leave a complete audit trail:
- Every waived finding appears in the report with its justification
- The `--format json` output includes all waiver metadata
- Version control on waiver files provides change history
