#!/usr/bin/env python3
"""Download and verify Plant OS upstream dependencies; safe to run repeatedly."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "scripts"))
from sources import Sources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    ports = ("mesa", "openjdk", "minecraft", "lwjgl", "term")
    parser.add_argument("components", nargs="*", choices=ports, default=list(ports),
                        help="dependency groups (default: all)")
    args = parser.parse_args()
    for name in args.components:
        dependencies = Sources(ROOT / "apps" / name)
        for component in dependencies.specs:
            print(dependencies[component].prepare(), flush=True)


if __name__ == "__main__":
    main()
