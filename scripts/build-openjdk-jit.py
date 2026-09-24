#!/usr/bin/env python3
"""Configure and build the native x86_64 C1/C2 OpenJDK from pinned sources."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
from sources import ROOT, Sources, digest, publish


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "apps/out/x86_64/openjdk")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    dependencies = Sources(ROOT / "apps/openjdk")
    source = dependencies["openjdk"].prepare()
    boot = dependencies["bootjdk"].prepare()
    apps = ROOT / "apps"
    runtime = apps / "out/x86_64"
    build = args.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    with (build / ".build-lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        environment = dict(os.environ, PLANT_OPENJDK_BUILD_OS="linux")
        for variable in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEOVERRIDES", "MAKELEVEL"):
            environment.pop(variable, None)
        subprocess.run(["make", "-f", "dynamic.mk", "ARCH=x86_64",
                        f"MESA_JOBS={args.jobs}", "dynamic", f"-j{args.jobs}"],
                       cwd=apps, env=environment, check=True)
        flags = subprocess.check_output(["make", "-s", "--no-print-directory", "-f", "dynamic.mk",
                                         "ARCH=x86_64", "print-runtime-flags"], cwd=apps, env=environment,
                                        text=True).strip()
        include = subprocess.check_output(["gcc", "-print-file-name=include"], text=True).strip()
        flags += f" -nostdinc -I{apps / 'include'} -isystem {include}"
        cxx = f"-nostdinc++ -I{runtime / 'mesa/libcxx/include/c++/v1'} " + flags + " -fno-exceptions -fno-rtti"
        options = [
            "--openjdk-target=x86_64-unknown-plantos", f"--with-boot-jdk={boot}",
            "--with-jvm-variants=server",
            "--with-jvm-features=compiler1,compiler2,serialgc,management,nmt,jfr,services,jvmti,"
            "-cds,-epsilongc,-g1gc,-jni-check,-jvmci,-parallelgc,-shenandoahgc,-vm-structs",
            "--enable-headless-only", "--with-freetype=bundled", "--disable-precompiled-headers",
            "--with-native-debug-symbols=none", "--disable-warnings-as-errors",
            f"--with-extra-cflags={flags}", f"--with-extra-cxxflags={cxx}",
            f"--with-extra-ldflags=-nostdlib -L{runtime / 'lib'} -Wl,-rpath-link,{runtime / 'lib'} "
            "-Wl,--no-as-needed -lp -lcpp",
        ]
        state = json.dumps({"options": options, "source": digest(source / ".plant-source-sha256"),
                            "gcc": subprocess.check_output(["gcc", "--version"], text=True),
                            "script": digest(Path(__file__))}, sort_keys=True)
        if not (build / "spec.gmk").exists() or not (build / ".plant-config").exists() or (build / ".plant-config").read_text() != state:
            subprocess.run(["bash", str(source / "configure"), *options], cwd=build, env=environment, check=True)
            publish(build / ".plant-config", state)
        subprocess.run(["make", "-C", str(build), "jdk-image", f"JOBS={args.jobs}"], env=environment, check=True)
        image = build / "images/jdk"
        if not (image / "lib/server/libjvm.so").is_file():
            raise RuntimeError("Server VM image was not produced")
        print(f"Native C1/C2 JDK image: {image}")


if __name__ == "__main__":
    main()
