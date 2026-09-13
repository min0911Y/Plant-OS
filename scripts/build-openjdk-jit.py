#!/usr/bin/env python3
"""Build native x86_64 C1/C2 HotSpot using an existing Plant OpenJDK 17 build."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True,
                        help="configured Plant OpenJDK 17 build directory")
    parser.add_argument("--out", type=Path,
                        help="JDK image directory (default: BUILD/images/jdk-jit)")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    build = args.build.resolve()
    output = (args.out or build / "images/jdk-jit").resolve()
    spec = dict(re.findall(r"^([A-Z_]+)[ \t]*:=[ \t]*(.*)$",
                           (build / "spec.gmk").read_text(), re.MULTILINE))
    if (spec.get("OPENJDK_TARGET_OS_ENV") != "plantos" or
            spec.get("OPENJDK_TARGET_CPU") != "x86_64" or
            spec.get("VERSION_FEATURE") != "17"):
        parser.error("--build must be configured for Plant OS x86_64 OpenJDK 17")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    base_image = build / "images/jdk"
    if not (base_image / "lib/modules").is_file():
        parser.error("build the base JDK image first; see doc/openjdk.md")
    if output.is_relative_to(base_image) or base_image.is_relative_to(output):
        parser.error("--out must be separate from the base JDK image")
    if output.exists() and not (output / "lib/modules").is_file():
        parser.error("--out already exists and is not a JDK image")
    source = Path(spec["TOPDIR"])
    patches = Path(__file__).resolve().parents[1] / "apps/openjdk/patches"
    for name in ("plant-x86-hotspot.patch", "plant-launcher-execname.patch",
                 "plant-process-environment.patch", "plant-nio-paths.patch"):
        patch_command = ["patch", "--batch", "-p1", "-d", str(source), "-i", str(patches / name)]
        applicable = subprocess.run([*patch_command, "--dry-run", "--forward"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if applicable.returncode == 0:
            subprocess.run([*patch_command, "--forward"], check=True)
        else:
            subprocess.run([*patch_command, "--dry-run", "--reverse"], check=True)
    library_dir = build / "hotspot/variant-server/libjvm"
    subprocess.run([
        "make", "-C", str(build), "hotspot-server", "java.base-libs", "JVM_VARIANTS=server",
        "JVM_VARIANT_MAIN=server", "JVM_FEATURES_server=compiler1 compiler2 serialgc",
        "HOTSPOT_TARGET_CPU=x86_64", "HOTSPOT_TARGET_CPU_ARCH=x86",
        f"JVM_LIB_OUTPUTDIR={library_dir}",
        f"JOBS={args.jobs}",
    ], check=True)
    for target in ("java.base-java-only", "java.base-jmod-only", "jdk-image-only"):
        subprocess.run(["make", "-C", str(build), target, f"JOBS={args.jobs}"], check=True)
    output.mkdir(parents=True, exist_ok=True)
    # JDK legal files include read-only files and relative symlinks. Replace
    # directory entries when updating, rather than writing through those links.
    subprocess.run(["cp", "-a", "--remove-destination", str(base_image) + "/.", str(output)],
                   check=True)
    (output / "lib/server").mkdir(exist_ok=True)
    shutil.copy2(library_dir / "libjvm.so", output / "lib/server/libjvm.so")
    (output / "lib/jvm.cfg").write_text("-server KNOWN\n-client IGNORE\n")
    print(f"Native C1/C2 JDK image: {output}")
    print("Build success does not establish JIT runtime support; run the guest regressions.")


if __name__ == "__main__":
    main()
