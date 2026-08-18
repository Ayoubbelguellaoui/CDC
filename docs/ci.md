# CI Integration

OpenCDC is designed for continuous integration pipelines.

## Exit Codes

| Code | Meaning |
|---|---|
| 0 | No unsuppressed error findings |
| 1 | One or more unsuppressed error findings |
| 2 | User/configuration/input error |
| 3 | Internal tool failure |

## JSON Schema

The `--format json` output is a JSON array of finding objects:

```json
[
  {
    "rule_id": "CDC001",
    "rule_name": "unsynchronized_crossing",
    "severity": "error",
    "waived": false,
    "source": "mod.src_ff",
    "source_domain": "clk_a",
    "dest": "mod.dst_ff",
    "dest_domain": "clk_b",
    "reason": "Register 'mod.src_ff' in domain 'clk_a' drives register 'mod.dst_ff' in domain 'clk_b' without synchronization.",
    "file": "design.sv",
    "line": 42
  }
]
```

## GitHub Actions

```yaml
- name: Run CDC Check
  run: |
    ./build/src/opencdc check design.sv --top top_module --format json --out cdc_report.json
    # Exit code 1 fails the step if unsuppressed errors found
```

## GitLab CI

```yaml
cdc_check:
  script:
    - ./build/src/opencdc check design.sv --top top_module --format json --out cdc_report.json
  artifacts:
    paths:
      - cdc_report.json
    when: always
```

## Integrating with Waivers

```bash
# Run with waivers — waived errors don't fail CI
opencdc check design.sv --top top --waiver waivers.txt --format json --out report.json
```

## Disabling Rules

```bash
# Disable specific rules for CI
opencdc check design.sv --top top --disable-rule CDC003
```

## Severity Overrides

```bash
# Promote warnings to errors (fail CI on warnings)
opencdc check design.sv --top top --severity CDC003=error
```

## Best Practices

1. **Always use `--format json`** in CI for machine-parseable output
2. **Use `--out`** to save reports as artifacts
3. **Maintain waiver files** in version control with justification
4. **Pin the OpenCDC version** in CI to avoid unexpected behavior changes
5. **Run on every PR** to catch new CDC crossings early
