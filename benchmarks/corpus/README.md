# OpenCDC Benchmark Corpus

Realistic SystemVerilog designs for measuring CDC-detection quality:
correct designs, known violations, async FIFOs, handshake protocols,
generated/gated/muxed clocks, hierarchical designs, blackbox IP, and
false-positive-prone tricky cases.

The legacy synthetic fixtures live in `benchmarks/fixtures/` (8 files,
`cdc001–cdc008_benchmark.sv`). The realistic corpus lives here, in
`benchmarks/corpus/`, one directory per category:

| Category | File(s) | What it proves |
|---|---|---|
| `clean/` | `sync_2ff_clean.sv` | Correct 2FF crossing: only CDC001 info/verified_safe |
| `violations/` | `unsync_single_bit.sv` | CDC001 error, nothing else |
| `violations/` | `unsync_bus.sv` | CDC001+CDC002 on a raw bus and a binary counter |
| `violations/` | `reconvergence_fanout.sv` | CDC003 (+CDC001/CDC002 on the skewed legs) |
| `violations/` | `comb_into_sync.sv` | Comb in sync feed surfaces as CDC001; CDC006 ambiguous (unreachable from single-driver RTL) |
| `violations/` | `missing_reset_crossing.sv` | CDC007 on a reset-less 2FF |
| `fifo/` | `async_fifo_gray.sv` | Correct gray-pointer FIFO: no CDC002, but CDC001 error on the wide pointer chains (width==1 sync limitation) |
| `fifo/` | `async_fifo_binary_broken.sv` | Binary pointers: CDC001+CDC002 on both legs |
| `handshake/` | `handshake_4phase_valid_ready.sv` | Correct 4-phase: controls clean, payload flagged (control-pair-only verification) |
| `handshake/` | `handshake_2phase_req_ack.sv` | Correct toggle 2-phase: same split, no CDC012 |
| `handshake/` | `pulse_synchronizer.sv` | Clean pulse-via-toggle, no CDC011 |
| `clocks/` | `generated_div_clock.sv` | Divide-by-4 traced to parent root: zero findings by design |
| `clocks/` | `gated_and_muxed_clocks.sv` | CDC004 + CDC005 positives, muxed-with-reset negative |
| `hierarchical/` | `hierarchical_soc.sv` | Traced top-reg path + untraced instance-to-instance violation (false negative) |
| `blackbox/` | `blackbox_crossing_top.sv` + `vendor_cdc_ip.yaml` | Same RTL run twice: visible without the model, attributed to the safe black box with it |
| `tricky/` | `fp_prone_crossings.sv` | Quasi-static config (FP), constant tie-off (no edge), clean reset sync, mixed-reset CDC009 |

## Running

```bash
cmake --build build -j$(nproc)
OPENCDC_BIN=build/src/opencdc python3 benchmarks/scripts/run_benchmark.py \
    benchmarks/expected/manifest.json
```

## Manifest schema (v1.2)

Each entry under `benchmarks/`: `file`, `top`, `description`, plus
`expected` with any of:

- `positive_controls`: `[{rule, source, dest, reason_contains?, note}]` —
  a finding must match rule + source/dest substrings (+ reason substring).
- `negative_controls`: same shape + `expected: "no_finding"` — no finding
  may match.
- `ambiguous_controls`: reported, never fail — for genuine limitations and
  judgment calls (`expected` is free-form: `undecided`, `fires-today`, …).
- `present_rules` / `absent_rules`: rule must have ≥1 / zero findings
  anywhere in the fixture. Needed for clean designs, where info-level
  CDC001 findings legitimately exist so pair-controls can't express
  "clean".
- `config` / `waiver` / `constraints`: project-root-relative support
  files passed as `--config` / `--waiver` / `--constraints`.
- `args`: extra raw CLI flags.

The runner fails a fixture if the tool itself errors or times out, so a
zero-finding fixture (e.g. `corpus_genclk`) means analyzed-and-clean.

## Ground-truth policy

Every expectation was verified against actual tool output before being
encoded; fixture headers carry `[CORPUS NOTE — verified YYYY-MM-DD]`
markers explaining *why* the tool behaves that way, with code
references. If a tool fix changes behavior, the fixture header and the
`known_gaps` manifest section must be updated together — a newly passing
positive is a product improvement, not a benchmark failure to edit away.
