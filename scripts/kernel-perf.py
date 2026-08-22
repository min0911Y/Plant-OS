#!/usr/bin/env python3
import argparse
import bisect
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path


SAMPLE_RE = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)(?:\s+(.*))?\s*$")
SYMBOL_RE = re.compile(r"^([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)$")


def load_symbols(kernel):
    try:
        proc = subprocess.run(
            ["nm", "-n", str(kernel)],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"failed to run nm on {kernel}: {exc}")

    symbols = []
    for line in proc.stdout.splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        addr = int(match.group(1), 16)
        typ = match.group(2).lower()
        name = match.group(3).split()[0]
        if typ in {"t", "w"} and name:
            symbols.append((addr, name))

    if not symbols:
        raise SystemExit(f"no text symbols found in {kernel}")

    symbols.sort()
    return symbols


def symbolize(symbols, addr):
    addrs = [item[0] for item in symbols]
    index = bisect.bisect_right(addrs, addr) - 1
    if index < 0:
        return f"0x{addr:08x}"

    base, name = symbols[index]
    offset = addr - base
    if offset:
        return f"{name}+0x{offset:x}"
    return name


def iter_perf_samples(serial_path):
    in_dump = False

    with open(serial_path, "r", encoding="utf-8", errors="replace") as file:
        for line in file:
            if "PERF_BEGIN" in line:
                in_dump = True
                continue
            if "PERF_END" in line:
                break
            if not in_dump:
                continue

            match = SAMPLE_RE.match(line)
            if not match:
                continue

            depth = int(match.group(3))
            rest = match.group(4) or ""
            pcs = []
            for token in rest.split()[:depth]:
                try:
                    pcs.append(int(token, 16))
                except ValueError:
                    break
            if pcs:
                yield pcs


def find_flamegraph():
    exe = shutil.which("flamegraph.pl")
    if exe:
        return exe

    candidates = [
        Path("FlameGraph/flamegraph.pl"),
        Path("../FlameGraph/flamegraph.pl"),
        Path("/opt/FlameGraph/flamegraph.pl"),
    ]
    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def write_folded(kernel, serial, out):
    symbols = load_symbols(kernel)
    stacks = Counter()
    sample_count = 0

    for pcs in iter_perf_samples(serial):
      names = [symbolize(symbols, pc) for pc in pcs]
      names.reverse()
      stacks[";".join(names)] += 1
      sample_count += 1

    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="utf-8") as file:
        for stack, count in sorted(stacks.items()):
            file.write(f"{stack} {count}\n")

    return sample_count, len(stacks)


def maybe_write_svg(folded, svg):
    flamegraph = find_flamegraph()
    if not flamegraph:
        print(
            "flamegraph.pl not found; generate SVG with: "
            f"flamegraph.pl {folded} > {svg}",
            file=sys.stderr,
        )
        return False

    with open(folded, "r", encoding="utf-8") as src, open(svg, "w", encoding="utf-8") as dst:
        subprocess.run([flamegraph, "--title", "Plant OS boot perf"], check=True, stdin=src, stdout=dst)
    return True


def main():
    parser = argparse.ArgumentParser(description="Convert Plant OS boot perf serial dump to folded stacks.")
    parser.add_argument("--kernel", required=True, type=Path, help="kernel ELF, usually kernel/obj/kernel.bin")
    parser.add_argument("--serial", required=True, type=Path, help="QEMU serial log containing PERF_BEGIN/PERF_END")
    parser.add_argument("--out", required=True, type=Path, help="folded stack output path")
    parser.add_argument("--svg", type=Path, help="optional flamegraph SVG output path")
    parser.add_argument("--no-svg", action="store_true", help="only write folded stacks")
    args = parser.parse_args()

    sample_count, stack_count = write_folded(args.kernel, args.serial, args.out)
    print(f"wrote {args.out} from {sample_count} samples ({stack_count} folded stacks)")

    if sample_count == 0:
        print("warning: no samples found between PERF_BEGIN and PERF_END", file=sys.stderr)

    if not args.no_svg:
        svg = args.svg or args.out.with_suffix(".svg")
        if maybe_write_svg(args.out, svg):
            print(f"wrote {svg}")


if __name__ == "__main__":
    main()
