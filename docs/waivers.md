# Waivers

Waivers suppress known-safe CDC findings. They provide an auditable trail of reviewed crossings.

## Waiver File Format

Each waiver is a single line with space-separated fields:

```
RULE_ID SOURCE DEST SOURCE_DOMAIN DEST_DOMAIN "JUSTIFICATION" OWNER [EXPIRY]
```

### Fields

| Field | Required | Description |
|-------|----------|-------------|
| RULE_ID | Yes | Rule ID (e.g., CDC001) |
| SOURCE | Yes | Source register name (substring match) |
| DEST | Yes | Destination register name (substring match) |
| SOURCE_DOMAIN | Yes | Source clock domain (empty = any) |
| DEST_DOMAIN | Yes | Destination clock domain (empty = any) |
| JUSTIFICATION | Yes | Quoted reason for the waiver |
| OWNER | Yes | Who approved the waiver (e.g., @team) |
| EXPIRY | No | Expiry date (YYYY-MM-DD) |

### Example

```yaml
# waivers.txt
CDC001 mod.src mod.dst clk_a clk_b "Known safe crossing reviewed by team" @reviewer 2027-12-31
CDC002 bus.src bus.dst clk_x clk_y "Gray-coded bus" @designer
```

## CLI Usage

```bash
opencdc check design.sv --top top --waiver waivers.txt
```

## Config File Usage

```yaml
waivers:
  - rule: CDC001, source: mod.src, dest: mod.dst, justification: "Known safe", owner: "@team"
  - rule: CDC002, source: bus.src, dest: bus.dst, justification: "Gray-coded", owner: "@designer", expiry: "2027-12-31"
```

## Matching Rules

- **Case-insensitive**: All field comparisons are case-insensitive
- **Substring match**: Source and dest names are matched as substrings (e.g., "src" matches "mod.src_ff")
- **Empty fields**: Empty source/dest/domain fields match any value
- **Expiry**: Waivers with expired dates are automatically skipped

## Audit Trail

Waived findings are still included in the output with:
- `waived: true`
- `waiver_justification`: The reason text
- `waiver_owner`: Who approved it

This allows reviewers to see what was waived and why.
