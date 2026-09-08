#!/usr/bin/env python3
"""Compare two --cube output directories, including their rendered pixels."""
import argparse
import json
from pathlib import Path
import re


def load(directory):
    text = (directory / "serial.log").read_text(errors="replace")
    config = re.search(r"VKCUBE CONFIG device=(.*?) cpus=(\d+) workers=(\S+) size=(\d+x\d+)", text)
    rows = re.findall(r"VKCUBE BENCH round=(\d+) frames=(\d+) elapsed_ns=(\d+) render_ns=(\d+) "
                      r"fps=([\d.]+) median_ms=([\d.]+) p95_ms=([\d.]+)", text)
    if not config or "VKCUBE PASS" not in text or len(rows) != 3:
        raise ValueError(f"incomplete cube run: {directory}")
    if [int(row[0]) for row in rows] != list(range(3)):
        raise ValueError(f"invalid sample sequence: {directory}")
    frames = sum(int(row[1]) for row in rows)
    elapsed = sum(int(row[2]) for row in rows)
    rendering = sum(int(row[3]) for row in rows)
    if not frames or not elapsed:
        raise ValueError(f"empty timing sample: {directory}")
    return {"device": config[1], "cpus": int(config[2]), "workers": config[3],
            "size": config[4], "frames": frames, "fps": frames * 1e9 / elapsed,
            "render_ms": rendering / frames / 1e6,
            "round_fps": [float(row[4]) for row in rows]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--max-regression", type=float, default=0.05,
                        help="allowed FPS loss as a fraction (default 0.05)")
    parser.add_argument("--out", type=Path, help="write the comparison as JSON")
    args = parser.parse_args()
    if not 0 <= args.max_regression < 1:
        parser.error("--max-regression must be in [0, 1)")
    before, after = load(args.before), load(args.after)
    if json.loads((args.before / "configuration.json").read_text()) != json.loads((args.after / "configuration.json").read_text()):
        raise ValueError("QEMU configurations differ; rerun with identical firmware, CPU, memory and accelerator")
    for key in ("device", "cpus", "size", "frames"):
        if before[key] != after[key]:
            raise ValueError(f"incompatible {key}: {before[key]} != {after[key]}")
    for phase in range(2):
        image = f"cube-{phase}.rgb"
        if (args.before / image).read_bytes() != (args.after / image).read_bytes():
            raise ValueError(f"rendered pixels differ: {image}")
    change = after["fps"] / before["fps"] - 1
    passed = change >= -args.max_regression
    result = {"before": before, "after": after, "fps_change": change,
              "pixels_identical": True, "max_regression": args.max_regression,
              "passed": passed}
    if args.out:
        args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{before['frames']} frames each; {before['fps']:.3f} -> {after['fps']:.3f} FPS "
          f"({change:+.2%}); render {before['render_ms']:.3f} -> {after['render_ms']:.3f} ms/frame; "
          f"pixels identical; {'PASS' if passed else 'REGRESSION'}")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
