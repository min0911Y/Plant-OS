#!/usr/bin/env python3
"""Run the LWJGL core/GLFW/OpenGL/STB client regression in Plant OS."""
import argparse
import importlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
APPS = ROOT / "apps"
LWJGL = APPS / "out/x86_64/lwjgl"
RESULT_GUEST = "::/java/lwjgl/lwjgl-result.txt"
FAILURE_GUEST = "::/java/lwjgl/lwjgl-failure.txt"
def count_color(pixels, predicate):
    return sum(predicate(pixels[index:index + 3])
               for index in range(0, len(pixels), 3))


def color_count(pixels, color):
    if color == "red":
        return count_color(pixels, lambda value: value[0] > 180 and value[1] < 80)
    return count_color(pixels, lambda value: value[1] > 180 and value[0] < 80)


def assert_frame(path, pixels, color):
    count = color_count(pixels, color)
    if count < 100:
        raise RuntimeError(f"{color} LWJGL frame has too few colored pixels: {count}")
    path.write_bytes(pixels)


def copy_guest_file(disk, guest_path, destination):
    result = subprocess.run(["mtype", "-i", str(disk), guest_path],
                            text=True, capture_output=True, check=False)
    if result.returncode != 0:
        return None
    destination.write_text(result.stdout)
    return result.stdout


def copy_failure_log(disk, output):
    copy_guest_file(disk, FAILURE_GUEST, output / "lwjgl-failure.txt")


def clear_outputs(output):
    for name in ("build.log", "test.iso", "test-jdk.img", "serial.log", "qemu.log",
                 "qmp.sock", "console.ppm", "console.png",
                 "lwjgl-probe.ppm", "lwjgl-frame-0.ppm", "lwjgl-frame-0.rgb",
                 "lwjgl-frame-1.ppm", "lwjgl-frame-1.rgb", "lwjgl-result.txt",
                 "lwjgl-failure.txt", "lwjgl-hs_err.log"):
        path = output / name
        try:
            path.unlink()
        except FileNotFoundError:
            pass


def run_guest(iso, disk, output, args):
    qmp_module = importlib.import_module("test-x86_64")
    QMP = qmp_module.QMP
    serial = output / "serial.log"
    qmp_path = output / "qmp.sock"
    cpu = "host" if args.accel == "kvm" else "max,-xgetbv1"
    command = ["qemu-system-x86_64", "-accel", args.accel, "-cpu", cpu,
               "-smp", "4", "-machine", "pc", "-m", str(args.memory),
               "-rtc", "base=2026-09-14T04:05:06,clock=vm", "-display", "none",
               "-serial", f"file:{serial}", "-monitor", "none", "-no-reboot",
               "-no-shutdown", "-cdrom", str(iso), "-boot", "d",
               "-drive", f"file={disk},format=raw,if=ide,index=0",
               "-qmp", f"unix:{qmp_path},server=on,wait=off"]
    with tempfile.TemporaryDirectory(prefix="plant-lwjgl-ovmf-") as temporary:
        if args.firmware == "uefi":
            variables = Path(temporary) / "vars.fd"
            shutil.copyfile(args.ovmf / "OVMF_VARS_4M.fd", variables)
            command += ["-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf / 'OVMF_CODE_4M.fd'}",
                        "-drive", f"if=pflash,format=raw,file={variables}"]
        with (output / "qemu.log").open("w") as qemu_log:
            guest = subprocess.Popen(command, stdout=qemu_log, stderr=subprocess.STDOUT)
            qmp = None
            sent_key = False
            checked_initial = False
            checked_final = False
            command_failed = False
            deadline = time.monotonic() + args.timeout
            probe_at = time.monotonic()
            try:
                while guest.poll() is None and time.monotonic() < deadline:
                    text = serial.read_text(errors="replace") if serial.exists() else ""
                    if "PANIC" in text or re.search(r"x86_64 exception .*cs=8", text):
                        raise RuntimeError(f"Plant kernel failed; see {serial}")
                    if qmp is None:
                        try:
                            qmp = QMP(qmp_path)
                        except (FileNotFoundError, ConnectionRefusedError, TimeoutError):
                            time.sleep(0.1)
                            continue
                    if re.search(r"task exit with code -?[1-9][0-9]*", text) and not command_failed:
                        qmp.screenshot(output / "console.ppm")
                        copy_failure_log(disk, output)
                        copy_guest_file(disk, "::/java/lwjgl/hs_err.log",
                                        output / "lwjgl-hs_err.log")
                        copy_guest_file(disk, RESULT_GUEST,
                                        output / "lwjgl-result.txt")
                        command_failed = True
                        raise RuntimeError(f"LWJGL Java command failed; see {output}")
                    if time.monotonic() >= probe_at:
                        _, _, pixels = qmp.screenshot(output / "lwjgl-probe.ppm")
                        probe_at = time.monotonic() + 0.25
                        if not checked_initial and color_count(pixels, "red") >= 100:
                            shutil.copyfile(output / "lwjgl-probe.ppm",
                                            output / "lwjgl-frame-0.ppm")
                            assert_frame(output / "lwjgl-frame-0.rgb", pixels, "red")
                            checked_initial = True
                        if checked_initial and not sent_key:
                            qmp.execute("input-send-event", events=[
                                {"type": "key", "data": {"down": True,
                                 "key": {"type": "qcode", "data": "a"}}}])
                            time.sleep(0.15)
                            qmp.execute("input-send-event", events=[
                                {"type": "key", "data": {"down": False,
                                 "key": {"type": "qcode", "data": "a"}}}])
                            sent_key = True
                        if sent_key and not checked_final and color_count(pixels, "green") >= 100:
                            shutil.copyfile(output / "lwjgl-probe.ppm",
                                            output / "lwjgl-frame-1.ppm")
                            assert_frame(output / "lwjgl-frame-1.rgb", pixels, "green")
                            checked_final = True
                    if "acpi: entering S5" in text:
                        if qmp.execute("query-status")["status"] == "shutdown":
                            qmp.execute("quit")
                            guest.wait(timeout=10)
                            break
                    time.sleep(0.1)
                else:
                    if qmp is not None and guest.poll() is None:
                        qmp.screenshot(output / "console.ppm")
                    raise RuntimeError(f"LWJGL guest did not shut down; see {output}")
                if guest.returncode != 0:
                    raise RuntimeError(f"QEMU failed with status {guest.returncode}; see {output}")
            finally:
                if qmp is not None:
                    qmp.close()
                if guest.poll() is None:
                    guest.kill()
                guest.wait()
    text = serial.read_text(errors="replace")
    copy_guest_file(disk, "::/java/lwjgl/hs_err.log", output / "lwjgl-hs_err.log")
    result = copy_guest_file(disk, RESULT_GUEST, output / "lwjgl-result.txt")
    stages = set(result.splitlines()) if result is not None else set()
    missing = [stage for stage in ("JNI", "CORE", "CALLBACK", "GLFW", "OPENGL", "STB")
               if stage not in stages]
    if (missing or not checked_initial or not checked_final or
            "acpi: entering S5" not in text or
            not any("C:/java/bin/java" in line and "status=0" in line
                    for line in text.splitlines())):
        raise RuntimeError(f"LWJGL regression failed; missing={missing}; "
                           f"stages={sorted(stages)}; see {serial}")
    if (output / "lwjgl-frame-0.rgb").read_bytes() == (output / "lwjgl-frame-1.rgb").read_bytes():
        raise RuntimeError("LWJGL frame did not change after the A key callback")
    print(f"LWJGL JNI/CORE/CALLBACK/GLFW/OPENGL/STB PASS: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jdk", type=Path, required=True, help="Plant OpenJDK 17 image")
    parser.add_argument("--javac", default=str(APPS / "out/host/openjdk/jdk-17.0.2/bin/javac"))
    parser.add_argument("--firmware", choices=("bios", "uefi"), default="bios")
    parser.add_argument("--accel", choices=("tcg", "kvm"), default="tcg")
    parser.add_argument("--ovmf", type=Path, default=Path("/usr/share/OVMF"))
    parser.add_argument("--memory", type=int, default=4096)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--out", type=Path, default=Path("/tmp/plant-lwjgl"))
    args = parser.parse_args()
    if not args.jdk.is_dir():
        parser.error(f"JDK directory does not exist: {args.jdk}")
    if not (args.jdk / "release").is_file() or not (args.jdk / "bin/java").is_file():
        parser.error(f"--jdk must be a Plant JDK home, not a regression output directory: {args.jdk}")
    if args.firmware == "uefi":
        for name in ("OVMF_CODE_4M.fd", "OVMF_VARS_4M.fd"):
            if not (args.ovmf / name).is_file():
                parser.error(f"missing UEFI firmware: {args.ovmf / name}")

    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    clear_outputs(output)
    build_log = output / "build.log"
    iso = output / "test.iso"
    disk = output / "test-jdk.img"
    init = ROOT / "kernel/res/init.mst"
    original = init.read_bytes()
    command = "lua.bin C:/java/lwjgl/run-lwjgl.lua"
    init.write_text('"todo" = [\n' +
                    '{"action" = "run" "command_line" = "' +
                    command.replace('"', '\\"') + '"},\n' +
                    '{"action" = "run" "command_line" = "psh.bin -c shutdown"}\n]\n')
    try:
        with build_log.open("w") as log:
            subprocess.run(["make", "-C", str(APPS), "ARCH=x86_64", "lwjgl", "-j8"],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(["make", "-C", str(ROOT / "kernel"), "ARCH=x86_64", "-j8"],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            environment = dict(
                os.environ,
                JAVAC=str(Path(args.javac).resolve()),
                PLANT_OPENJDK_DIR=str(args.jdk.resolve()),
                PLANT_OPENJDK_DISK=str(disk),
                PLANT_LWJGL_DIR=str(LWJGL.resolve()),
            )
            subprocess.run([str(ROOT / "scripts/build-livecd.sh"), str(iso), "x86_64"],
                           env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    finally:
        init.write_bytes(original)
    (output / "configuration.json").write_text(json.dumps({
        "firmware": args.firmware, "accel": args.accel, "memory_mib": args.memory,
        "classpath": "explicit native directory", "lwjgl_version": "3.3.6",
    }, indent=2) + "\n")
    run_guest(iso, disk, output, args)


if __name__ == "__main__":
    main()
