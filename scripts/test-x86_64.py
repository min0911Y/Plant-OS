#!/usr/bin/env python3
"""Boot Plant's x86 regressions through init.mst; use QMP only to test GUI input."""

import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time


class QMP:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(5)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb")
        json.loads(self.stream.readline())
        self.execute("qmp_capabilities")

    def execute(self, command, **arguments):
        self.stream.write(json.dumps({"execute": command, "arguments": arguments}).encode() + b"\n")
        self.stream.flush()
        while True:
            reply = json.loads(self.stream.readline())
            if "error" in reply:
                raise RuntimeError(f"QMP {command}: {reply['error']}")
            if "return" in reply:
                return reply["return"]

    def close(self):
        self.stream.close()
        self.socket.close()

    def move(self, dx, dy):
        # Small packets avoid PS/2 overflow and retain exact relative coordinates.
        remaining = [dx, dy]
        while any(remaining):
            steps = [max(-100, min(100, delta)) for delta in remaining]
            events = [{"type": "rel", "data": {"axis": axis, "value": step}}
                      for axis, step in zip(("x", "y"), steps) if step]
            self.execute("input-send-event", events=events)
            remaining = [delta - step for delta, step in zip(remaining, steps)]
            time.sleep(0.15)

    def press(self, event_type, **data):
        for down in (True, False):
            self.execute("input-send-event", events=[
                {"type": event_type, "data": dict(data, down=down)}])
            time.sleep(0.15)

    def screenshot(self, path):
        self.execute("screendump", filename=str(path))
        magic, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
        width, height = map(int, dimensions.split())
        if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
            raise RuntimeError(f"invalid QEMU screenshot: {path}")
        return width, height, pixels


def exercise_mouse(qmp_path, origin, target, output):
    qmp = QMP(qmp_path)
    try:
        qmp.execute("screendump", filename=str(output / "mouse-before.ppm"))
        qmp.move(target[0] - origin[0], target[1] - origin[1])
        for button in ("left", "right", "wheel-up"):
            qmp.press("btn", button=button)
        qmp.execute("screendump", filename=str(output / "mouse-after.ppm"))
    finally:
        qmp.close()


def exercise_console(qmp_path, output):
    qmp = QMP(qmp_path)
    try:
        width, height, _ = qmp.screenshot(output / "console-desktop.ppm")
        # Focus the initial console, whose text is initially behind the toolbox.
        qmp.move(700 - width // 2, 500 - height // 2)
        qmp.press("btn", button="left")

        def cell_rows(label, row, columns):
            width, height, pixels = qmp.screenshot(output / f"console-{label}.ppm")
            x, y = 250 + 4, 250 + 24 + row * 16
            if x + columns * 8 > width or y + 16 > height:
                raise RuntimeError("initial console is outside the display")
            return b"".join(pixels[(line * width + x) * 3:
                                   (line * width + x + columns * 8) * 3]
                            for line in range(y, y + 16))

        deadline = time.monotonic() + 10
        while True:
            prompt = cell_rows("prompt", 1, 7)
            if len(set(prompt)) > 1:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("GUI console has no shell prompt")
            time.sleep(0.2)
        if len(set(cell_rows("banner", 0, 13))) <= 1:
            raise RuntimeError("GUI console has no shell banner")
        before = cell_rows("before-input", 1, 12)
        qmp.press("key", key={"type": "qcode", "data": "a"})
        if cell_rows("echo", 1, 12) == before:
            raise RuntimeError("GUI console keyboard input produced no echo")
        qmp.press("key", key={"type": "qcode", "data": "backspace"})
        if cell_rows("backspace", 1, 12) != before:
            raise RuntimeError("GUI console backspace did not restore the prompt")
        # Submit only empty lines: guest commands are always selected by init.mst.
        for _ in range(30):
            qmp.press("key", key={"type": "qcode", "data": "ret"})
        if cell_rows("scrolled-top", 0, 7) != prompt:
            raise RuntimeError("GUI console did not scroll completed lines")
        if cell_rows("scrolled-bottom", 24, 7) != prompt:
            raise RuntimeError("GUI console lost its prompt after scrolling")
    finally:
        qmp.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x86_64", "i386"), default="x86_64")
    parser.add_argument("--firmware", choices=("bios", "uefi"), default="bios")
    parser.add_argument("--memory", type=int, default=1024, help="guest RAM in MiB")
    parser.add_argument("--cpus", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=120)
    gui_mode = parser.add_mutually_exclusive_group()
    gui_mode.add_argument("--capacity", action="store_true", help="also cross the 255-task boundary")
    gui_mode.add_argument("--mouse", action="store_true", help="validate PS/2 motion, buttons and wheel using QMP")
    gui_mode.add_argument("--console", action="store_true", help="validate GUI shell rendering, input echo, backspace and scrolling")
    parser.add_argument("--out", type=Path, default=Path("/tmp/plant-x86_64-smoke"))
    parser.add_argument("--ovmf", type=Path, default=Path("/usr/share/OVMF"))
    args = parser.parse_args()
    if args.arch == "i386" and args.firmware == "uefi":
        parser.error("the i386 LiveCD uses BIOS")
    repo = Path(__file__).resolve().parent.parent
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    native = args.arch == "x86_64"
    commands = (["archtest.bin", "cpptest.bin", "simdtest.bin"] if native else ["fputest.bin"]) + [
        "exc_test.bin", "rpctest.bin", "dktest.bin", "nettest.bin loopback",
        "guitest.bin capacity" if args.capacity else "guitest.bin mouse" if args.mouse else "guitest.bin",
        "psh.bin -c insmod hello.mod", "psh.bin -c rmmod hello_mod",
        "psh.bin -c insmod hello.mod", "psh.bin -c rmmod hello_mod",
        'lua.bin -e assert(math.sqrt(81)==9);assert(math.abs(math.sin(0.5)-0.479425538604203)<1e-12)',
    ]
    expected = (["ARCHTEST PASS", "CPPTEST PASS", "SIMDTEST PASS"] if native else ["FPUTEST PASS"]) + [
                "EXCEPTION_TEST done checks=5 fails=0", "GUITEST THREAD PASS", "GMOUSE ID =",
                "RPCTEST done checks=22 fails=0", "DKTEST PASS",
                "GUISTRESS PASS" if args.capacity else "GUITEST PASS"]
    if args.mouse:
        expected.append("GUIMOUSE PASS events=15")
    if args.console:
        commands = ["gui.bin"]
    iso = repo / "kernel" / ("plant-os-x86_64.iso" if native else "plant-os-livecd.iso")
    init = repo / "kernel/res/init.mst"
    original = init.read_bytes()
    def mst_quote(text):
        return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'
    actions = [f'  {{"action" = "run" "command_line" = {mst_quote(command)}}}'
               for command in commands + ["psh.bin"]]
    with (output / "build.log").open("w") as build_log:
        try:
            init.write_text('"todo" = [\n' + ',\n'.join(actions) + '\n]\n')
            subprocess.run(["make", "-C", str(repo / "apps"), f"ARCH={args.arch}"],
                           stdout=build_log, stderr=subprocess.STDOUT, check=True)
            if not native:
                subprocess.run(["make", "-C", str(repo / "loader")],
                               stdout=build_log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(["make", "-C", str(repo / "kernel"), f"ARCH={args.arch}", "livecd", "-j8"],
                           stdout=build_log, stderr=subprocess.STDOUT, check=True)
        finally:
            init.write_bytes(original)
    serial = output / "serial.log"
    serial.write_bytes(b"")
    command = ["qemu-system-x86_64", "-accel", "tcg", "-cpu", "max", "-smp", str(args.cpus),
               "-m", str(args.memory), "-cdrom", str(iso),
               "-boot", "d", "-display", "none", "-serial", f"file:{serial}",
               "-monitor", "none", "-no-reboot", "-no-shutdown", "-netdev", "user,id=net0",
               "-device", "pcnet,netdev=net0"]
    try:
        with tempfile.TemporaryDirectory(prefix="plant-ovmf-") as temporary:
            qmp_path = Path(temporary) / "qmp.sock"
            command += ["-qmp", f"unix:{qmp_path},server=on,wait=off"]
            if args.firmware == "uefi":
                variables = Path(temporary) / "vars.fd"
                shutil.copyfile(args.ovmf / "OVMF_VARS_4M.fd", variables)
                command += ["-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf / 'OVMF_CODE_4M.fd'}",
                            "-drive", f"if=pflash,format=raw,file={variables}"]
            with (output / "qemu.log").open("w") as qemu_log:
                guest = subprocess.Popen(command, stdout=qemu_log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + args.timeout
                    mouse_sent = False
                    while time.monotonic() < deadline and guest.poll() is None:
                        text = serial.read_text(errors="replace") if serial.exists() else ""
                        if re.search(r"PANIC|x86_64 exception .*cs=8", text):
                            raise RuntimeError(f"kernel failure; see {serial}")
                        if args.console and "GMOUSE ID =" in text:
                            exercise_console(qmp_path, output)
                            print(f"{args.arch} {args.firmware} GUICONSOLE PASS: prompt, echo, backspace, scroll; {output}")
                            return
                        mouse = re.search(r"GUIMOUSE READY origin=(\d+),(\d+) target=(\d+),(\d+)", text)
                        if args.mouse and mouse and not mouse_sent:
                            origin = tuple(map(int, mouse.group(1, 2)))
                            target = tuple(map(int, mouse.group(3, 4)))
                            exercise_mouse(qmp_path, origin, target, output)
                            mouse_sent = True
                        if "init: run psh.bin\n" in text:
                            statuses = re.findall(r"^init: command .* status=(-?\d+)$", text, re.M)
                            if len(statuses) != len(commands) or any(status != "0" for status in statuses):
                                raise RuntimeError(f"command regression; see {serial}")
                            missing = [marker for marker in expected if marker not in text]
                            if missing:
                                raise RuntimeError(f"missing {missing}; see {serial}")
                            print(f"{args.arch} {args.firmware} PASS: {args.cpus} CPUs, {args.memory} MiB; {serial}")
                            return
                        time.sleep(0.2)
                    raise RuntimeError(f"guest stopped or timed out; see {serial}")
                finally:
                    guest.terminate()
                    try:
                        guest.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        guest.kill()
                        guest.wait()
    finally:
        # Leave the normal boot script in the distributable image too.
        with (output / "restore.log").open("w") as restore_log:
            restore = ([str(repo / "scripts/build-livecd.sh"), str(iso), args.arch]
                       if native else ["make", "-C", str(repo / "kernel"),
                                       "ARCH=i386", "livecd", "-j8"])
            subprocess.run(restore,
                           stdout=restore_log, stderr=subprocess.STDOUT, check=True)


if __name__ == "__main__":
    main()
