# CI Integration

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | No unsuppressed error findings |
| 1 | One or more unsuppressed error findings |
| 2 | User/configuration/input error |
| 3 | Internal tool failure |

## GitHub Actions

```yaml
name: CDC Check

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  cdc-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Build OpenCDC
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j$(nproc)

      - name: Run CDC Check
        run: |
          ./build/src/opencdc check rtl/top.sv --top top \
            --config opencdc.yaml \
            --format json --out cdc_report.json
        continue-on-error: false

      - name: Upload Report
        uses: actions/upload-artifact@v4
        if: always()
        with:
          name: cdc-report
          path: cdc_report.json
```

## GitLab CI

```yaml
cdc-check:
  stage: lint
  script:
    - cmake -B build -DCMAKE_BUILD_TYPE=Release
    - cmake --build build -j$(nproc)
    - ./build/src/opencdc check rtl/top.sv --top top --config opencdc.yaml
  artifacts:
    when: always
    paths:
      - cdc_report.json
    reports:
      codequality: cdc_report.json
```

## Best Practices

1. **Run on every PR**: Catch CDC issues before merge
2. **Use waivers for known-safe crossings**: Maintain a waiver file in the repo
3. **Fail the build on errors**: Use exit code 1 to block merges
4. **Archive reports**: Upload JSON reports as artifacts for traceability
5. **Disable noisy rules**: Use `--disable-rule` for rules that don't apply to your design

## Badge

Add a status badge to your README:

```markdown
![CDC Check](https://github.com/your-org/your-repo/actions/workflows/cdc-check.yml/badge.svg)
```
