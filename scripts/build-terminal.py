#!/usr/bin/env python3
"""Build the pinned terminal library with the native byte-stream ABI."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from sources import ROOT, Sources, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("i386", "x86_64"), required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    source = Sources(ROOT / "apps/term")["terminal"].prepare()
    output = ROOT / "apps/out" / ("x86_64" if args.arch == "x86_64" else "") / "terminal"
    target = str(ROOT / f"apps/term/{args.arch}-plantos.json")
    environment = dict(os.environ, CARGO_TARGET_DIR=str(output),
                       FONT_PATH=str(source / "fonts/FiraCodeNotoSans.woff2"),
                       RUSTFLAGS="-C relocation-model=pic -C no-redzone=yes")
    subprocess.run(["cargo", "+nightly", "build", "--locked", "--release", "--features", "embedded-font",
                    "--target", target, "-j", str(args.jobs)], cwd=source, env=environment, check=True)
    library = output / Path(target).stem / "release/libos_terminal.a"
    destination = output / "libos_terminal.a"
    if not destination.exists() or digest(library) != digest(destination):
        shutil.copyfile(library, destination)


if __name__ == "__main__":
    main()
