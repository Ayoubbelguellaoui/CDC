#!/usr/bin/env python3
"""
OpenCDC Benchmark Runner
Runs CDC analysis on benchmark fixtures and compares actual vs expected results.
Reports per-rule TP/FP/TN/FN metrics.
"""

import json
import subprocess
import sys
import os
from pathlib import Path
from dataclasses import dataclass, field


@dataclass
class RuleMetrics:
    rule_id: str
    tp: int = 0
    fp: int = 0
    fn: int = 0
    tn: int = 0

    @property
    def precision(self) -> float:
        denom = self.tp + self.fp
        return self.tp / denom if denom > 0 else 0.0

    @property
    def recall(self) -> float:
        denom = self.tp + self.fn
        return self.tp / denom if denom > 0 else 0.0

    @property
    def f1(self) -> float:
        p, r = self.precision, self.recall
        return 2 * p * r / (p + r) if (p + r) > 0 else 0.0


def run_opencdc(binary, fixture, top):
    cmd = [binary, "check", fixture, "--top", top, "--format", "json"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if result.returncode > 1:
        return [], result.stderr
    try:
        findings = json.loads(result.stdout) if result.stdout.strip() else []
    except json.JSONDecodeError:
        return [], f"JSON parse error: {result.stdout[:200]}"
    return findings, result.stderr


def main():
    project_root = str(Path(__file__).resolve().parent.parent.parent)
    binary = os.environ.get("OPENCDC_BIN", os.path.join(project_root, "build/src/opencdc"))
    manifest_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        project_root, "benchmarks/expected/manifest.json")

    if not os.path.exists(binary):
        print(f"ERROR: Binary not found: {binary}")
        print("Build first: cmake --build build -j$(nproc)")
        sys.exit(1)

    with open(manifest_path) as f:
        manifest = json.load(f)

    benchmarks = manifest.get("benchmarks", {})
    results = []
    rule_metrics = {}

    for fixture_key, fixture in benchmarks.items():
        fixture_path = os.path.join(project_root, fixture["file"])
        top_module = fixture["top"]
        expected = fixture.get("expected", {})

        actual_findings, stderr = run_opencdc(binary, fixture_path, top_module)

        actual_rules = set(f["rule_id"] for f in actual_findings)

        positives = expected.get("positive_controls", [])
        negatives = expected.get("negative_controls", [])

        tp = 0
        fn = 0
        for pos in positives:
            rule = pos["rule"]
            src = pos.get("source", "")
            dst = pos.get("dest", "")

            found = False
            for af in actual_findings:
                if af["rule_id"] == rule:
                    if src and src not in af.get("source", ""):
                        continue
                    if dst and dst not in af.get("dest", ""):
                        continue
                    found = True
                    break

            if found:
                tp += 1
            else:
                fn += 1

            if rule not in rule_metrics:
                rule_metrics[rule] = RuleMetrics(rule_id=rule)
            rm = rule_metrics[rule]
            if found:
                rm.tp += 1
            else:
                rm.fn += 1

        for neg in negatives:
            rule = neg["rule"]
            expected_behavior = neg.get("expected", "no_finding")
            src = neg.get("source", "")
            dst = neg.get("dest", "")

            found = False
            for af in actual_findings:
                if af["rule_id"] == rule:
                    if src and src not in af.get("source", ""):
                        continue
                    if dst and dst not in af.get("dest", ""):
                        continue
                    found = True
                    break

            if rule not in rule_metrics:
                rule_metrics[rule] = RuleMetrics(rule_id=rule)
            rm = rule_metrics[rule]

            if expected_behavior == "no_finding":
                if not found:
                    rm.tn += 1
                else:
                    rm.fp += 1
                    fn += 1
            elif expected_behavior == "cdc002_fires":
                if found:
                    rm.tp += 1
                    tp += 1
                else:
                    rm.fn += 1
                    fn += 1

        passed = fn == 0
        results.append({
            "name": fixture_key,
            "passed": passed,
            "tp": tp,
            "fn": fn,
            "actual_count": len(actual_findings),
            "positives": len(positives),
            "negatives": len(negatives),
        })

    print("=" * 72)
    print("OpenCDC Benchmark Results")
    print("=" * 72)

    total_pass = 0
    total_fail = 0
    for r in results:
        status = "PASS" if r["passed"] else "FAIL"
        print(f"  [{status}] {r['name']}: TP={r['tp']} FN={r['fn']} actual_findings={r['actual_count']}")
        if r["passed"]:
            total_pass += 1
        else:
            total_fail += 1

    print()
    print("-" * 72)
    print("Per-Rule Metrics")
    print("-" * 72)
    print(f"  {'Rule':<10} {'TP':>4} {'FP':>4} {'FN':>4} {'TN':>4} {'Prec':>7} {'Recall':>7} {'F1':>7}")
    for rule_id in sorted(rule_metrics.keys()):
        rm = rule_metrics[rule_id]
        print(f"  {rm.rule_id:<10} {rm.tp:>4} {rm.fp:>4} {rm.fn:>4} {rm.tn:>4} "
              f"{rm.precision:>7.3f} {rm.recall:>7.3f} {rm.f1:>7.3f}")

    print()
    print("-" * 72)
    total_tp = sum(rm.tp for rm in rule_metrics.values())
    total_fp = sum(rm.fp for rm in rule_metrics.values())
    total_fn = sum(rm.fn for rm in rule_metrics.values())
    total_tn = sum(rm.tn for rm in rule_metrics.values())
    overall_p = total_tp / (total_tp + total_fp) if (total_tp + total_fp) > 0 else 0.0
    overall_r = total_tp / (total_tp + total_fn) if (total_tp + total_fn) > 0 else 0.0
    overall_f1 = 2 * overall_p * overall_r / (overall_p + overall_r) if (overall_p + overall_r) > 0 else 0.0

    print(f"  OVERALL: TP={total_tp} FP={total_fp} FN={total_fn} TN={total_tn} "
          f"Precision={overall_p:.3f} Recall={overall_r:.3f} F1={overall_f1:.3f}")
    print(f"  Fixtures: {total_pass} passed, {total_fail} failed, {len(results)} total")
    print("=" * 72)

    print()
    print("Known Gaps:")
    for gap in manifest.get("known_gaps", []):
        print(f"  [{gap['severity'].upper()}] {gap['rule']}: {gap['gap']}")

    sys.exit(1 if total_fail > 0 else 0)


if __name__ == "__main__":
    main()
