#!/usr/bin/env python3
"""Package the native JIT JDK and unmodified Minecraft into an auto-start image."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
from sources import ROOT, Sources, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jdk", type=Path, default=ROOT / "apps/out/x86_64/openjdk/images/jdk")
    parser.add_argument("--server", type=Path)
    parser.add_argument("--iso", type=Path, default=ROOT / "kernel/plant-os-x86_64-minecraft.iso")
    parser.add_argument("--disk", type=Path, default=ROOT / "kernel/plant-os-x86_64-minecraft.img")
    parser.add_argument("--disk-mib", type=int, default=1536)
    parser.add_argument("--config", type=Path, default=ROOT / "apps/minecraft")
    args = parser.parse_args()
    dependency = Sources(ROOT / "apps/minecraft")["server"]
    server = args.server.resolve() if args.server else dependency.prepare()
    if digest(server) != dependency.spec["sha256"]:
        parser.error("Minecraft server JAR does not match the pinned version")
    if not (args.jdk / "lib/server/libjvm.so").is_file():
        parser.error("build the native Server JDK first: make -C kernel ARCH=x86_64 openjdk")
    if args.disk_mib < 768:
        parser.error("Minecraft disk needs at least 768 MiB")
    # Existing disks may contain real worlds. Building images never erases one.
    if args.disk.exists():
        parser.error(f"disk already exists: {args.disk}; choose another output or explicitly remove it")
    work = ROOT / "apps/out/x86_64/minecraft"
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="payload-", dir=work) as temporary:
        payload = Path(temporary)
        shutil.copyfile(server, payload / "server.jar")
        for name in ("server.properties", "run-minecraft.lua"):
            shutil.copyfile(args.config / name, payload / name)
        (payload / "eula.txt").write_text("eula=true\n")
        # Pre-extract byte-identical bundled dependencies to avoid slow guest
        # first-boot writes. The original bundler still verifies their hashes.
        with zipfile.ZipFile(server) as archive:
            for group in ("libraries", "versions"):
                for line in archive.read(f"META-INF/{group}.list").decode().splitlines():
                    checksum, _, relative = line.split("\t")
                    path = Path(relative)
                    if path.is_absolute() or ".." in path.parts:
                        raise RuntimeError(f"invalid bundled path: {relative}")
                    target = payload / group / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.read(f"META-INF/{group}/{relative}"))
                    if digest(target) != checksum:
                        raise RuntimeError(f"bundled hash mismatch: {relative}")
        environment = dict(os.environ, PLANT_OPENJDK_DIR=str(args.jdk.resolve()),
                           PLANT_OPENJDK_DISK=str(args.disk.resolve()),
                           PLANT_OPENJDK_DISK_MIN_MIB=str(args.disk_mib),
                           PLANT_MINECRAFT_DIR=str(payload),
                           PLANT_INIT_SCRIPT=str(ROOT / "apps/minecraft/init.mst"))
        subprocess.run([str(ROOT / "scripts/build-livecd.sh"), str(args.iso.resolve()), "x86_64"],
                       env=environment, check=True)


if __name__ == "__main__":
    main()
