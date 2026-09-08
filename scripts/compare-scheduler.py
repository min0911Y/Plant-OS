#!/usr/bin/env python3
"""Compare matching schbench.bin runs produced by test-x86_64.py scheduler benchmarks."""
import argparse
import json
from pathlib import Path
import statistics

BENCHMARKS = {
    "SCHEDBENCH": (("before", "parked", "after"),
                   ("handoff_ns", "p50_ns", "p95_ns", "p99_ns", "yield_ns", "compute_ns"),
                   ("sleepers", "cpus", "rounds", "work")),
    "SCHEDBALANCE": (("balance",), ("elapsed_ns",),
                     ("cpus", "jobs", "long_jobs", "short_work", "long_work")),
}


def read_run(directory):
    configuration = json.loads((directory / "configuration.json").read_text())
    serial = (directory / "serial.log").read_text(errors="replace")
    suites = [name for name in BENCHMARKS if serial.count(name + " PASS") == 1]
    if len(suites) != 1 or "SCHEDBENCH FAIL" in serial:
        raise ValueError(f"incomplete or failed benchmark: {directory}")
    suite = suites[0]
    phases, metrics, _ = BENCHMARKS[suite]
    records = {}
    for line in serial.splitlines():
        if not line.startswith(suite + " ") or line == suite + " PASS":
            continue
        fields = dict(field.split("=", 1) for field in line.split()[1:])
        phase = fields.pop("phase", "balance")
        record = {key: int(value) for key, value in fields.items()}
        if phase not in phases or any(
                record.get(metric, 0) <= 0 for metric in metrics):
            raise ValueError(f"invalid measurement: {line}")
        key = phase, record["repeat"]
        if key in records or record["cpus"] != configuration["cpus"]:
            raise ValueError(f"duplicate or inconsistent measurement: {line}")
        records[key] = record
    repeats = {repeat for phase, repeat in records}
    if (not repeats or repeats != set(range(1, max(repeats) + 1)) or
            set(records) != {(phase, repeat) for phase in phases
                             for repeat in repeats}):
        raise ValueError(f"missing phases or repetitions: {directory}")
    return suite, configuration, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        suite, baseline_config, baseline = read_run(args.baseline)
        candidate_suite, candidate_config, candidate = read_run(args.candidate)
        if (suite != candidate_suite or baseline_config != candidate_config or
                baseline.keys() != candidate.keys()):
            raise ValueError("configuration or repetition counts differ")
        phases, metrics, workload = BENCHMARKS[suite]
        for key in baseline:
            if any(baseline[key][field] != candidate[key][field]
                   for field in workload):
                raise ValueError("workloads differ")
        rows = []
        for phase in phases:
            for metric in metrics:
                old = [record[metric] for key, record in baseline.items() if key[0] == phase]
                new = [record[metric] for key, record in candidate.items() if key[0] == phase]
                old_median, new_median = statistics.median(old), statistics.median(new)
                row = dict(phase=phase, metric=metric, samples=len(old),
                           baseline_median_ns=old_median, candidate_median_ns=new_median,
                           baseline_range_ns=[min(old), max(old)],
                           candidate_range_ns=[min(new), max(new)],
                           reduction_percent=100 * (1 - new_median / old_median))
                rows.append(row)
                print(f"{phase:7} {metric:12} {old_median:12.0f} -> {new_median:12.0f} ns"
                      f"  reduction={row['reduction_percent']:+.2f}%")
        if args.out:
            args.out.write_text(json.dumps(dict(benchmark=suite, configuration=baseline_config, results=rows),
                                           indent=2) + "\n")
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
