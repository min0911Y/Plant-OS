#!/usr/bin/env python3
"""Report verified --compute-bench samples from identical guest configurations."""
import argparse
import json
from pathlib import Path
import re
import statistics


def load(directory):
    configuration = json.loads((directory / "configuration.json").read_text())
    text = (directory / "serial.log").read_text(errors="replace")
    if "LVPSCALE FAIL" in text or "LVPTEST FAIL" in text:
        raise ValueError(f"failed compute run: {directory}")
    runs = []
    current = None
    for line in text.splitlines():
        if line.startswith("LVPSCALE CONFIG "):
            if current is not None:
                raise ValueError(f"incomplete compute run: {directory}")
            values = dict(re.findall(r"(\w+)=(\w+)", line))
            current = {"workers": int(values.pop("workers")), "samples_ns": []}
            current["workload"] = values.pop("workload")
            current.update({key: int(value) for key, value in values.items()})
        elif line.startswith("LVPSCALE SAMPLE "):
            match = re.fullmatch(r"LVPSCALE SAMPLE round=(\d+) elapsed_ns=(\d+)", line)
            if current is None or not match or int(match[1]) != len(current["samples_ns"]) or int(match[2]) <= 0:
                raise ValueError(f"invalid compute sample: {line}")
            current["samples_ns"].append(int(match[2]))
        elif line.startswith("LVPSCALE PASS "):
            match = re.fullmatch(r"LVPSCALE PASS workers=(\d+) verified_blocks=(\d+)", line)
            if (current is None or not match or int(match[1]) != current["workers"] or
                    len(current["samples_ns"]) != current["rounds"] or
                    int(match[2]) != current["blocks"] * current["rounds"] * current["batches"]):
                raise ValueError(f"incomplete verification: {line}")
            runs.append(current)
            current = None
    if current is not None or [run["workers"] for run in runs] != [0, 1, 2, 4, 4, 2, 1, 0]:
        raise ValueError(f"missing forward/reverse runs: {directory}")
    if any(run["cpus"] != configuration["cpus"] for run in runs):
        raise ValueError(f"guest CPU count differs: {directory}")
    return configuration, runs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, help="save configuration, raw samples and summary as JSON")
    parser.add_argument("--min-speedup", type=float, default=0,
                        help="require four workers to beat both serial modes by this factor")
    args = parser.parse_args()
    if not 0 <= args.min_speedup < float("inf"):
        parser.error("--min-speedup must be finite and nonnegative")
    configuration, runs = load(args.directories[0])
    for directory in args.directories[1:]:
        other, samples = load(directory)
        if other != configuration:
            raise ValueError("guest configurations differ")
        runs.extend(samples)
    workload = {key: value for key, value in runs[0].items() if key not in ("workers", "samples_ns")}
    for run in runs:
        if any(run[key] != value for key, value in workload.items()):
            raise ValueError("benchmark workloads differ")
    summary = {}
    for workers in (0, 1, 2, 4):
        samples = [sample for run in runs if run["workers"] == workers for sample in run["samples_ns"]]
        dispatches = len(samples) * workload["batches"]
        elapsed = sum(samples)
        per_dispatch = [sample / workload["batches"] / 1e6 for sample in samples]
        summary[workers] = {"dispatches": dispatches, "elapsed_ns": elapsed,
                            "mean_ms": elapsed / dispatches / 1e6,
                            "median_ms": statistics.median(per_dispatch),
                            "min_ms": min(per_dispatch), "max_ms": max(per_dispatch),
                            "mib_per_second": workload["bytes"] * dispatches * 1e9 / elapsed / 2**20}
    print("workers  mean ms/dispatch  MiB/s    vs synchronous  vs 1 worker")
    for workers, row in summary.items():
        row["speedup_vs_synchronous"] = summary[0]["mean_ms"] / row["mean_ms"]
        row["speedup_vs_one_worker"] = summary[1]["mean_ms"] / row["mean_ms"]
        print(f"{workers:7d}  {row['mean_ms']:16.3f}  {row['mib_per_second']:7.2f}"
              f"  {row['speedup_vs_synchronous']:14.3f}x  {row['speedup_vs_one_worker']:10.3f}x")
    passed = min(summary[4]["speedup_vs_synchronous"], summary[4]["speedup_vs_one_worker"]) >= args.min_speedup
    result = {"configuration": configuration, "directories": [str(path) for path in args.directories],
              "workload": workload, "runs": runs, "summary": summary,
              "all_outputs_verified": True, "min_speedup": args.min_speedup, "passed": passed}
    if args.out:
        args.out.write_text(json.dumps(result, indent=2) + "\n")
    print("Full output verification PASS; speedup threshold " + ("PASS" if passed else "FAIL"))
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
