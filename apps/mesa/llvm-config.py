#!/usr/bin/env python3
"""Expose the native LLVM build metadata to Meson without executing target code."""
import json
from pathlib import Path
import shlex
import sys

output = Path(sys.argv[1])
port = Path(__file__).resolve().parent
spec = json.loads((port / "sources.json").read_text())["llvm"]
source = port.parent / "out/sources" / spec["directory"] / "llvm"
build = output / "mesa/llvm"
archives = json.loads((build / "components.json").read_text())
components = {Path(name).name[7:-2].lower() for name in archives}
components.update(("engine", "native", "nativecodegen", "all"))
queries = {
    "--version": spec["version"],
    "--components": " ".join(sorted(components)),
    "--has-rtti": "NO", "--shared-mode": "static", "--targets-built": "X86",
    "--host-target": "x86_64-unknown-none-elf",
    "--prefix": str(build), "--libdir": str(build / "lib"),
    "--includedir": str(source / "include"),
    "--cppflags": shlex.join(["-I" + str(source / "include"), "-I" + str(build / "include")]),
    "--libs": "-lLLVMPlant", "--libnames": "libLLVMPlant.a",
    "--libfiles": str(build / "lib/libLLVMPlant.a"),
    "--ldflags": shlex.join(["-L" + str(build / "lib")]),
    "--system-libs": shlex.join([str(output / "lib/libcpp.so"), str(output / "lib/libp.so")]),
}
result = []
for argument in sys.argv[2:]:
    if argument in queries:
        result.append(queries[argument])
    elif argument != "--link-static" and argument not in components:
        sys.exit(f"unsupported native LLVM query or component: {argument}")
print("\n".join(result))
