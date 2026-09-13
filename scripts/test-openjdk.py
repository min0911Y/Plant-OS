#!/usr/bin/env python3
"""Run native OpenJDK startup and NIO regressions on a separate test disk."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jdk", type=Path, required=True, help="Plant JDK image")
    parser.add_argument("--javac", required=True, help="host JDK 17+ javac")
    parser.add_argument("--out", type=Path, default=Path("/tmp/plant-openjdk-nio"))
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--accel", choices=("tcg", "kvm"), default="tcg")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    classes = output / "classes"
    classes.mkdir(exist_ok=True)
    subprocess.run([args.javac, "--release", "17", "-d", str(classes),
                    str(repo / "apps/openjdk/Startup.java"),
                    str(repo / "apps/openjdk/Nio.java")], check=True)
    iso = output / "test.iso"
    disk = output / "test-jdk.img"
    init = repo / "kernel/res/init.mst"
    original = init.read_bytes()
    with (output / "build.log").open("w") as log:
        subprocess.run(["make", "-C", str(repo / "apps"), "ARCH=x86_64", "-j8"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["make", "-C", str(repo / "kernel"), "ARCH=x86_64", "-j8"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        try:
            commands = [
                'C:/java/bin/java -Xms16m -Xmx128m -Djava.net.preferIPv4Stack=true -cp C:/missing;C:/java Nio C:/java/nio-result.txt',
                'psh.bin -c shutdown',
            ]
            def quote(value):
                return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
            init.write_text('"todo" = [\n' + ',\n'.join(
                '{"action" = "run" "command_line" = ' + quote(command) + '}'
                for command in commands) + '\n]\n')
            environment = dict(os.environ, PLANT_OPENJDK_DIR=str(args.jdk.resolve()),
                               PLANT_OPENJDK_DISK=str(disk))
            subprocess.run([str(repo / "scripts/build-livecd.sh"), str(iso), "x86_64"],
                           env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        finally:
            init.write_bytes(original)
    for path in classes.glob("*.class"):
        subprocess.run(["mcopy", "-o", "-i", str(disk), str(path), "::/java/"], check=True)
    # The result must be produced by this boot, even if the supplied JDK image
    # happens to contain an earlier test's output.
    subprocess.run(["mdel", "-i", str(disk), "::/java/nio-result.txt"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    serial = output / "serial.log"
    command = ["qemu-system-x86_64", "-accel", args.accel, "-cpu",
               "host" if args.accel == "kvm" else "max,-xgetbv1", "-smp", "4",
               "-m", "2048", "-display", "none", "-monitor", "none", "-no-reboot",
               "-qmp", f"unix:{output / 'qmp.sock'},server=on,wait=off",
               "-serial", f"file:{serial}", "-cdrom", str(iso), "-boot", "d",
               "-drive", f"file={disk},format=raw,if=ide,index=0"]
    with (output / "qemu.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                       timeout=args.timeout, check=True)
    result = subprocess.check_output(["mtype", "-i", str(disk), "::/java/nio-result.txt"], text=True)
    (output / "result.txt").write_text(result)
    text = serial.read_text(errors="replace")
    if (result != "OPENJDK NIO PASS\n" or "acpi: entering S5" not in text or
            "Nio C:/java/nio-result.txt status=0" not in text):
        raise RuntimeError(f"OpenJDK regression failed: {result.strip()}; see {output}")
    print(f"OPENJDK NIO PASS: {output}")


if __name__ == "__main__":
    main()
