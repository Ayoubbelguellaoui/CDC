# OpenCDC

Open-source **static** Clock Domain Crossing (CDC) analyzer for Verilog and
SystemVerilog. It walks an IR graph of registers and clocks and reports
structural crossings. It is **not** a formal CDC tool (SpyGlass CDC, Questa
CDC): it does not prove protocol correctness or the absence of metastability.

Accuracy of detection (false positives / false negatives on real SoCs) is
the product. Surrounding features (HTML, SARIF, LSP, Python) are secondary.

## What is verified vs guessed

| Proven structurally | Heuristic / shape match | Not covered |
|---|---|---|
| Register-to-register path between different root clocks | Gray-code encoder of the form `x ^ (x >> 1)` | Formal handshake / FIFO protocol |
| Single-bit 2FF / 3FF / N-stage synchronizer chain at the destination (destination-wide for a gray-coded source) | Async-FIFO pointer *shape* (gray pointers + synced r/w) | Payload data guarded by a handshake |
| ICG as `AND(clk, en)` (RTL) and library ICG cells (netlist) | Handshake *shape* (valid/ready or req/ack pair only) | Recirculation-mux / mux-feedback synchronizers |
| False paths and multicycle paths **if declared** (SDC / YAML) | Gated / muxed clocks via explicit `assign` | Timing / metastability proof |
| Muxed-clock and gated-clock *presence* on a crossing | Generated / divided clocks folded into the parent root | |
| Crossings through instance ports, in both directions, and clocks out of clock-gate instances | | |

Details: [docs/what-we-can-prove.md](docs/what-we-can-prove.md).

## Known limitations

These are measured gaps, not a wish list. Evidence lives in
`benchmarks/expected/manifest.json` (`known_gaps`) and the corpus fixtures.

- **Wide 2FF chains** are classified as synchronizers only for a gray-coded
  source that reaches the chain through nets and ports (async-FIFO pointers);
  a wide chain of any other bus still reports CDC001 error, by design.
- **Handshake payload** is not covered. A valid/ready (or req/ack) pair can
  be recognized; the data bus it guards still fires CDC001 + CDC002.
- **CDC006** (comb between sync stages) is effectively unreachable on
  single-driver RTL — the bug surfaces as CDC001 instead.
- **Generated / divided clocks** are folded into the parent root domain. No
  crossing is reported; phase relationship is **not** verified.
- **Clock gating / muxing** is recognized on explicit `assign` (and netlist
  ICG/mux cells). Net-initializer form is not.
- **Reset synchronizers**: a proper 2FF under a synchronized-but-different
  reset still reports CDC009.
- **No recirculation-mux synchronizer** recognition.
- **No formal verification** of gray, FIFO, or handshake protocols.
- OpenCDC has **not** been published against Ibex / OpenTitan / PULP with
  a manual FP/FN review. Fixture F1 on the in-tree corpus is not a substitute.

CDC005 (muxed clock without reset) and CDC013 (unknown-value propagation)
are **methodology / quality** checks, not core CDC. Default severity is
`warning`. Disable them if they are noise:

```bash
opencdc check design.sv --top top --disable-rule CDC005 --disable-rule CDC013
```

## Example output

Text report from a real run (`--format text --signoff --color always`) is
sectioned into Findings (severity-grouped, with rule ids, crossings, reasons,
and `file:line` locations), a human Summary, and a Signoff verdict.
JSON, SARIF, and interactive HTML are also available (`--format json|sarif|html`).

## Building

**Prerequisites:** C++20 compiler (GCC 11+ or Clang 14+), CMake 3.28+, Git.

Dependencies are fetched and **pinned**: slang `v8.0`, yaml-cpp `0.8.0`,
nlohmann/json `v3.11.3` (see `CMakeLists.txt`).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Usage

```bash
# Basic check (JSON on stdout)
opencdc check design.sv --top top_module

# Human-readable report
opencdc check design.sv --top top --format text --signoff --color auto

# Gate-level netlist
opencdc check synthesized.v --top top --input-type netlist --cell-map cells.yaml

# Config, waivers, false paths
opencdc check design.sv --top top --config opencdc.yaml --waiver waivers.txt
opencdc check design.sv --top top --false-path src_reg:meta_reg

# Includes / macros / jobs
opencdc check design.sv --top top --incdir rtl/include --define ENABLE_CDC --jobs 4
```

See `opencdc check --help` for the full flag list (`--format`, `--out`,
`--profile`, `--constraints`, `--liberty`, `--name-map`, `--save-baseline`, …).

## Rules

| ID | Name | Severity | Description |
|---|---|---|---|
| CDC001 | unsynchronized_crossing | error (info if verified 2FF/3FF) | Register drives register across domains without synchronization |
| CDC002 | multi_bit_crossing | error | Multi-bit bus crosses domains without gray-code or handshake *shape* |
| CDC003 | reconvergence_hazard | warning | Multiple paths from same source reconverge in destination domain |
| CDC004 | gated_clock_crossing | warning | Register clocked by gated clock crosses to another domain |
| CDC005 | muxed_clock_no_reset | warning | Methodology: muxed clock and no reset on a CDC path |
| CDC006 | combinational_between_sync | error | Combinational logic between synchronizer stages |
| CDC007 | missing_reset | info | CDC register without reset signal |
| CDC008 | multi_domain_daisy_chain | warning | Signal crosses 3+ clock domains in daisy chain |
| CDC009 | reset_domain_crossing | warning | Register crosses between asynchronous reset domains |
| CDC010 | path_traversal_truncated | warning | Analysis limits may have hidden additional crossings |
| CDC011 | pulse_crossing | warning | Pulse synchronizer without a proper 2FF chain |
| CDC012 | toggle_crossing | warning | Toggle synchronizer without a proper 2FF chain |
| CDC013 | unknown_propagation | warning | Methodology: uncertain value on an unsynchronized path |

Full reference: [docs/rules.md](docs/rules.md). Semantics vs other rules:
[docs/rule-semantics.md](docs/rule-semantics.md).

## Config file

```yaml
rules:
  CDC001:
    enabled: true
    severity: error
  CDC005:
    enabled: false
  CDC013:
    enabled: false

waivers:
  - rule: CDC001
    source: mod.src
    dest: mod.dst
    justification: "Known safe"
    owner: "@team"

false_paths:
  - source: mod.src_reg
    dest: mod.meta_reg

output:
  format: text
  file: report.txt
```

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | No unsuppressed error findings (or `--signoff` Pass / PassWithWaivers) |
| 1 | Unsuppressed error findings (or `--signoff` Fail / Incomplete / Error) |
| 2 | User / configuration / input error |
| 3 | Internal tool failure |

## Other capabilities

These exist and are tested; they do not substitute for detection accuracy.

- Gate-level **netlist** frontend (`--input-type netlist`) — [docs/netlist.md](docs/netlist.md)
- Waivers (substring / wildcard / regex) — [docs/waivers.md](docs/waivers.md)
- Baseline trend (`--save-baseline` / `--compare-baseline`)
- JSON, text, SARIF, interactive HTML reports
- Register inventories: the registers of each clock domain (`--clock-report <file>`) and of each reset signal, with the clock domains it reaches (`--reset-report <file>`)
- LSP server (`opencdc lsp`)
- Methodology profiles (`--profile asic_signoff|strict|fpga|…`)
- Optional Python bindings (`BUILD_PYTHON_BINDINGS=ON`)

## Documentation

- [What we can prove](docs/what-we-can-prove.md) — verified vs inferred vs unknown
- [Architecture](docs/architecture.md)
- [Netlist analysis](docs/netlist.md)
- [Rules](docs/rules.md) · [Rule semantics](docs/rule-semantics.md)
- [Waivers](docs/waivers.md)
- [CI integration](docs/ci.md)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Contact

- Repository: [https://github.com/Ayoubbelguellaoui/CDC](https://github.com/Ayoubbelguellaoui/CDC)
  (GitHub name is `CDC`; the project and binary are `opencdc`)
- Maintainer: Ayoub Belguellaoui — [belguellaoui01@gmail.com](mailto:belguellaoui01@gmail.com)

## License

Apache License 2.0. See [LICENSE](LICENSE).
