#!/usr/bin/env python3
"""Boot Plant's x86 regressions through init.mst; exercise devices through QMP."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import shutil
import socket
import struct
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

    def chord(self, *keys):
        for down, sequence in ((True, keys), (False, reversed(keys))):
            for key in sequence:
                self.execute("input-send-event", events=[
                    {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}])
                time.sleep(0.15)

    def screenshot(self, path):
        self.execute("screendump", filename=str(path))
        magic, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
        width, height = map(int, dimensions.split())
        if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
            raise RuntimeError(f"invalid QEMU screenshot: {path}")
        return width, height, pixels


def prepare_pci_fixture(qmp_path, gdb_path, qtest_path, kernel, ahci_no_irq=False):
    """Pause before device work, then alter only the selected QEMU fixture."""
    symbols = subprocess.check_output(["nm", "-n", str(kernel)], text=True)
    symbol = "ahci_execute" if ahci_no_irq else "xhci_initialize"
    match = re.search(rf"^([0-9a-f]+) [tT] {symbol}$", symbols, re.M)
    if not match:
        raise RuntimeError(f"{symbol} symbol missing")
    address = int(match[1], 16)
    deadline = time.monotonic() + 10
    while not all(path.exists() for path in (qmp_path, gdb_path, qtest_path)):
        if time.monotonic() >= deadline:
            raise RuntimeError("QEMU debug sockets unavailable")
        time.sleep(0.02)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as debugger:
        debugger.settimeout(60)
        debugger.connect(str(gdb_path))
        stream = debugger.makefile("rb")

        def packet(command):
            data = command.encode()
            debugger.sendall(b"$" + data + f"#{sum(data) & 255:02x}".encode())
            while True:
                prefix = stream.read(1)
                if not prefix:
                    raise RuntimeError("GDB connection closed")
                if prefix == b"$":
                    break
            reply = bytearray()
            while True:
                byte = stream.read(1)
                if not byte:
                    raise RuntimeError("GDB packet truncated")
                if byte == b"#":
                    break
                reply += byte
            checksum = stream.read(2)
            if len(checksum) != 2 or int(checksum, 16) != (sum(reply) & 255):
                raise RuntimeError("GDB checksum mismatch")
            debugger.sendall(b"+")
            return reply.decode()

        if packet(f"Z1,{address:x},1") != "OK" or not packet("c").startswith(("T05", "S05")):
            raise RuntimeError(f"did not stop at {symbol}")
        qmp = QMP(qmp_path)
        try:
            buses = qmp.execute("query-pci")
            devices = []
            identifiers = {"ahci-test"} if ahci_no_irq else {"usb2", "usb3"}
            pending = list(buses)
            while pending:
                entry = pending.pop()
                if isinstance(entry, list):
                    pending.extend(entry)
                elif isinstance(entry, dict):
                    if entry.get("qdev_id") in identifiers:
                        devices.append(entry)
                    pending.extend(value for value in entry.values()
                                   if isinstance(value, (dict, list)))
            if len(devices) != len(identifiers):
                raise RuntimeError("fixture PCI functions missing")
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as qtest:
                qtest.settimeout(5)
                qtest.connect(str(qtest_path))
                io = qtest.makefile("rwb")

                def request(command):
                    io.write(command.encode() + b"\n")
                    io.flush()
                    reply = io.readline().decode().strip()
                    if not reply.startswith("OK"):
                        raise RuntimeError(f"qtest {command}: {reply}")
                    return reply.split()[1:]

                saved = int(request("inl 0xcf8")[0], 0)
                try:
                    for device in devices:
                        if ahci_no_irq:
                            abar = next(region["address"] for region in device["regions"] if region["bar"] == 5)
                            ghc = int(request(f"readl {abar + 4:#x}")[0], 0)
                            if not ghc & 2:
                                raise RuntimeError("AHCI interrupt delivery was not enabled")
                            request(f"writel {abar + 4:#x} {ghc & ~2:#x}")
                            if int(request(f"readl {abar + 4:#x}")[0], 0) & 2:
                                raise RuntimeError("AHCI interrupt fault injection failed")
                            print("AHCI fixture: disabled GHC interrupt delivery before IDENTIFY", flush=True)
                            continue
                        bus, slot, function = device["bus"], device["slot"], device["function"]
                        selector = 0x8000003c | bus << 16 | slot << 11 | function << 8
                        request(f"outl 0xcf8 {selector:#x}")
                        request("outb 0xcfc 0xff")
                        if int(request("inb 0xcfc")[0], 0) != 255:
                            raise RuntimeError("PCI Interrupt Line write did not stick")
                        print(f"USB fixture {bus:02x}:{slot:02x}.{function}: PCI IRQ=255", flush=True)
                finally:
                    request(f"outl 0xcf8 {saved:#x}")
        finally:
            qmp.close()
        if packet(f"z1,{address:x},1") != "OK" or packet("D") != "OK":
            raise RuntimeError("could not resume QEMU after PCI fixture setup")


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


def exercise_console(qmp_path, output, origin=None):
    qmp = QMP(qmp_path)
    try:
        width, height, _ = qmp.screenshot(output / "console-desktop.ppm")
        # term centers its default 640x400 content in a decorated GUI window.
        origin = origin or (width // 2, height // 2)
        qmp.move(700 - origin[0], 500 - origin[1])
        qmp.press("btn", button="left")

        console_x, console_y = (width - 648) // 2, (height - 428) // 2

        def cell_rows(label, row, columns):
            width, height, pixels = qmp.screenshot(output / f"console-{label}.ppm")
            x, y = console_x + 4, console_y + 24 + row * 16
            if x + columns * 8 > width or y + 16 > height:
                raise RuntimeError("initial console is outside the display")
            return b"".join(pixels[(line * width + x) * 3:
                                   (line * width + x + columns * 8) * 3]
                            for line in range(y, y + 16))

        deadline = time.monotonic() + 10
        while True:
            prompt = cell_rows("prompt", 1, 7)
            if len(set(prompt[i:i + 3] for i in range(0, len(prompt), 3))) > 1:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("GUI console has no shell prompt")
            time.sleep(0.2)
        banner = cell_rows("banner", 0, 13)
        if len(set(banner[i:i + 3] for i in range(0, len(banner), 3))) <= 1:
            raise RuntimeError("GUI console has no shell banner")
        before = cell_rows("before-input", 1, 12)
        qmp.press("key", key={"type": "qcode", "data": "a"})
        if cell_rows("echo", 1, 12) == before:
            raise RuntimeError("GUI console keyboard input produced no echo")
        qmp.press("key", key={"type": "qcode", "data": "backspace"})
        if cell_rows("backspace", 1, 12) != before:
            raise RuntimeError("GUI console backspace did not restore the prompt")
        qmp.chord("ctrl", "shift", "f2")
        if cell_rows("theme-2", 1, 12) == before:
            raise RuntimeError("term theme shortcut did not change its pixels")
        qmp.chord("ctrl", "shift", "f1")
        if cell_rows("theme-1", 1, 12) != before:
            raise RuntimeError("term theme shortcut did not restore its palette")
        qmp.press("key", key={"type": "qcode", "data": "a"})
        if cell_rows("shortcut-release-echo", 1, 12) == before:
            raise RuntimeError("term retained shortcut modifiers after release")
        qmp.press("key", key={"type": "qcode", "data": "backspace"})
        if cell_rows("shortcut-release-backspace", 1, 12) != before:
            raise RuntimeError("term shortcut leaked into application input")
        # Submit only empty lines: guest commands are always selected by init.mst.
        for _ in range(30):
            qmp.press("key", key={"type": "qcode", "data": "ret"})
        if cell_rows("scrolled-top", 0, 7) != prompt:
            raise RuntimeError("GUI console did not scroll completed lines")
        if cell_rows("scrolled-bottom", 24, 7) != prompt:
            raise RuntimeError("GUI console lost its prompt after scrolling")
        for _ in range(8):
            qmp.press("btn", button="wheel-up")
        if cell_rows("history", 0, 13) != banner:
            raise RuntimeError("term did not reveal its scrollback history")
        for _ in range(8):
            qmp.press("btn", button="wheel-down")
        if cell_rows("history-bottom", 0, 7) != prompt:
            raise RuntimeError("term did not return from scrollback")
        qmp.chord("ctrl", "shift", "pgup")
        if cell_rows("keyboard-history", 0, 13) != banner:
            raise RuntimeError("term PageUp shortcut did not reveal history")
        qmp.chord("ctrl", "shift", "pgdn")
        if cell_rows("keyboard-history-bottom", 0, 7) != prompt:
            raise RuntimeError("term PageDown shortcut did not return from history")
        for _ in range(8):
            qmp.chord("ctrl_r", "shift_r", "up")
        if cell_rows("keyboard-history-lines", 0, 13) != banner:
            raise RuntimeError("term ArrowUp shortcut did not reveal history")
        for _ in range(8):
            qmp.chord("ctrl_r", "shift_r", "down")
        if cell_rows("keyboard-history-lines-bottom", 0, 7) != prompt:
            raise RuntimeError("term ArrowDown shortcut did not return from history")
        # Closing term must retire its blocked shell; the toolbox launches a
        # new process, window and independently owned TTY each time.
        mouse_x, mouse_y = 700, 500
        for cycle in range(2):
            close_x, close_y = console_x + 635, console_y + 15
            qmp.move(close_x - mouse_x, close_y - mouse_y)
            qmp.press("btn", button="left")
            if cell_rows(f"closed-{cycle}", 1, 7) == prompt:
                raise RuntimeError("closed GUI console is still visible")
            qmp.move(300 - close_x, 260 - close_y)
            qmp.press("btn", button="left")  # Toolbox / Terminal
            mouse_x, mouse_y = 300, 260
            deadline = time.monotonic() + 10
            while cell_rows(f"reopened-{cycle}", 1, 7) != prompt:
                if time.monotonic() >= deadline:
                    raise RuntimeError("reopened GUI console has no prompt")
                time.sleep(0.2)
            before = cell_rows(f"reopened-before-{cycle}", 1, 12)
            qmp.press("key", key={"type": "qcode", "data": "b"})
            if cell_rows(f"reopened-echo-{cycle}", 1, 12) == before:
                raise RuntimeError("reopened GUI console did not receive input")
            qmp.press("key", key={"type": "qcode", "data": "backspace"})
            if cell_rows(f"reopened-backspace-{cycle}", 1, 12) != before:
                raise RuntimeError("reopened GUI console backspace failed")
    finally:
        qmp.close()


def exercise_terminal_load(qmp_path, output):
    qmp = QMP(qmp_path)
    try:
        width, height, _ = qmp.screenshot(output / "terminal-load.ppm")
        qmp.move(700 - width // 2, 500 - height // 2)
        qmp.press("btn", button="left")
        x, y = (width - 648) // 2 + 4, (height - 428) // 2 + 40
        def prompt_pixels(label):
            _, _, pixels = qmp.screenshot(output / f"terminal-load-{label}.ppm")
            return b"".join(pixels[(row * width + x) * 3:
                                  (row * width + x + 12 * 8) * 3]
                            for row in range(y, y + 16))
        before = prompt_pixels("prompt")
        if len(set(before[i:i + 3] for i in range(0, len(before), 3))) <= 1:
            raise RuntimeError("loaded terminal has no shell prompt")
        qmp.press("key", key={"type": "qcode", "data": "a"})
        if prompt_pixels("echo") == before:
            raise RuntimeError("loaded terminal does not echo input")
        qmp.press("key", key={"type": "qcode", "data": "backspace"})
        if prompt_pixels("backspace") != before:
            raise RuntimeError("loaded terminal backspace failed")
    finally:
        qmp.close()


def exercise_editor(qmp_path, serial, output):
    qmp = QMP(qmp_path)

    def wait_for(marker, count=1):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            text = serial.read_text(errors="replace")
            if "EDITORTEST FAIL" in text or "PANIC" in text:
                raise RuntimeError(f"editor regression; see {serial}")
            if text.count(marker) >= count:
                return
            time.sleep(0.1)
        raise RuntimeError(f"editor did not reach {marker}; see {serial}")

    def key(code):
        qmp.press("key", key={"type": "qcode", "data": code})

    def type_text(text):
        codes = {" ": "spc", "/": "slash", ".": "dot"}
        for char in text:
            key(codes.get(char, char))

    def content(label, row=0, rows=23):
        width, height, pixels = qmp.screenshot(output / f"editor-{label}.ppm")
        left, top = (width - 648) // 2 + 4, (height - 428) // 2 + 24 + row * 16
        return b"".join(pixels[(y * width + left) * 3:(y * width + left + 640) * 3]
                        for y in range(top, top + rows * 16))

    def restored(phase):
        wait_for(f"EDITORTEST RESTORED phase={phase}")
        time.sleep(0.1)
        bar = content(f"restored-{phase}", 23, 1)
        if len(set(bar[i:i + 3] for i in range(0, len(bar), 3))) != 1:
            raise RuntimeError("editor did not restore the previous terminal screen")
        key("ret")

    try:
        wait_for("EDITOR ready", 1)
        width, height, _ = qmp.screenshot(output / "editor-desktop.ppm")
        qmp.move(700 - width // 2, 500 - height // 2)
        qmp.press("btn", button="left")
        before = content("opened")
        key("end")
        key("left")
        key("backspace")
        key("2")
        qmp.chord("ctrl", "z")
        qmp.chord("ctrl", "y")
        key("delete")
        qmp.chord("ctrl", "z")
        key("end")
        key("ret")
        qmp.chord("ctrl", "z")
        qmp.chord("ctrl", "y")
        type_text("// saved")
        if content("edited") == before:
            raise RuntimeError("editor received no visible text input")
        qmp.chord("ctrl", "f")
        type_text("value")
        key("ret")
        qmp.chord("ctrl", "n")
        qmp.chord("ctrl", "p")
        key("esc")
        with_lines = content("line-numbers")
        qmp.chord("ctrl", "r")
        if content("without-line-numbers") == with_lines:
            raise RuntimeError("editor line-number shortcut had no effect")
        qmp.chord("ctrl", "r")
        qmp.chord("ctrl", "s")
        content("saved")
        qmp.chord("ctrl", "q")
        restored(1)

        wait_for("EDITOR ready", 2)
        # Navigation on an empty document must leave a valid insertion point.
        for code in ("pgdn", "pgup", "home", "end"):
            key(code)
        type_text("draft")
        qmp.chord("ctrl", "s")
        type_text("missing/edsave.txt")
        key("ret")
        content("save-error", 24, 1)
        qmp.chord("ctrl", "s")
        type_text("edsave.txt")
        key("ret")
        qmp.chord("ctrl", "q")
        restored(2)

        wait_for("EDITOR ready", 3)
        key("x")
        before = content("modified", 24, 1)
        qmp.chord("ctrl", "q")
        if content("quit-warning", 24, 1) == before:
            raise RuntimeError("editor did not warn about unsaved changes")
        key("esc")
        for _ in range(4):
            qmp.chord("ctrl", "q")
        restored(3)

        wait_for("EDITOR ready", 4)
        type_text("new file")
        qmp.chord("ctrl", "s")
        qmp.chord("ctrl", "q")
        restored(4)
    finally:
        qmp.close()


def exercise_usb(qmp_path, serial, guest, deadline, keyboard_bus, keyboard_port, hubs):
    qmp = QMP(qmp_path)

    def wait_for(pattern, count=1):
        while time.monotonic() < deadline and guest.poll() is None:
            text = serial.read_text(errors="replace")
            if re.search(r"PANIC|xhci: .*failed|xhci: .*timeout|completion error", text):
                raise RuntimeError(f"USB failure; see {serial}")
            matches = re.findall(pattern, text, re.M)
            if len(matches) >= count:
                return matches
            time.sleep(0.02)
        raise RuntimeError(f"USB timeout waiting for {pattern}; see {serial}")

    try:
        for speed in ("full", "high", "super"):
            wait_for(rf"^usb: .* speed={speed} .* enumerated$")
        # USB audio advertises a 64-byte EP0 at full speed. This forces
        # Evaluate Context to update the initial 8-byte control packet size.
        wait_for(r"^usb: .* port=3 .* speed=full .* usb=0100 .* enumerated$")
        controllers = wait_for(r"^xhci: (\S+) .* ready$", 2)
        if len(set(controllers)) != 2:
            raise RuntimeError("USB test requires two independent xHCI controllers")
        # Each detach/attach submits Disable Slot, Enable Slot and Address
        # Device. 100 cycles cross both command and event ring boundaries.
        full = r"^usb: .* speed=full id=0627:0001 .* enumerated$"
        wait_for(full, 2)
        text = serial.read_text(errors="replace")
        connected = len(re.findall(full, text, re.M))
        removed = (r"^usb-hub: .* port=1 disconnected$" if hubs else
                   r"^usb: .* port=1 disconnected$")
        disconnected = len(re.findall(removed, text, re.M))
        for cycle in range(100):
            qmp.execute("device_del", id="usb-keyboard")
            wait_for(removed, disconnected + cycle + 1)
            qmp.execute("device_add", driver="usb-kbd", id="usb-keyboard",
                        bus=keyboard_bus, port=keyboard_port, usb_version=1)
            wait_for(full, connected + cycle + 1)
            if (cycle + 1) % 25 == 0:
                print(f"USB hotplug {cycle + 1}/100", flush=True)
        if hubs:
            # Removing a hub also removes its QEMU child devices; the guest
            # must tear down all descendant slots before releasing the hub.
            text = serial.read_text(errors="replace")
            root_removed = r"^usb: .* port=7 disconnected$"
            removed_count = len(re.findall(root_removed, text, re.M))
            active = r"^usb-hub: .* ports=8 .* active$"
            active_count = len(re.findall(active, text, re.M))
            keyboard = r"^usb: .* port=7 .* route=00012 .* enumerated$"
            mouse = r"^usb: .* port=7 .* route=00042 .* enumerated$"
            keyboard_count = len(re.findall(keyboard, text, re.M))
            mouse_count = len(re.findall(mouse, text, re.M))
            qmp.execute("device_del", id="usb-hub-root")
            wait_for(root_removed, removed_count + 1)
            qmp.execute("device_add", driver="usb-hub", id="usb-hub-root",
                        bus=keyboard_bus, port="3", **{"port-power": True})
            qmp.execute("device_add", driver="usb-hub", id="usb-hub-child",
                        bus=keyboard_bus, port="3.2", **{"port-power": True})
            qmp.execute("device_add", driver="usb-kbd", id="usb-keyboard",
                        bus=keyboard_bus, port=keyboard_port, usb_version=1)
            qmp.execute("device_add", driver="usb-mouse", bus=keyboard_bus, port="3.2.4")
            wait_for(active, active_count + 2)
            wait_for(keyboard, keyboard_count + 1)
            wait_for(mouse, mouse_count + 1)
            print("USBHUB PASS: two tiers, port power, routes, subtree reconnect", flush=True)
        print("USBTEST PASS: full/high/super-speed descriptors, EP0 resize, "
              "two controllers, 100 hotplugs, command/event ring wrap", flush=True)
    finally:
        qmp.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x86_64", "i386"), default="x86_64")
    parser.add_argument("--firmware", choices=("bios", "uefi"), default="bios")
    parser.add_argument("--machine", choices=("pc", "q35"), default="pc",
                        help="QEMU chipset; q35 exercises ACPI MCFG/ECAM configuration access")
    parser.add_argument("--memory", type=int, default=1024, help="guest RAM in MiB")
    parser.add_argument("--cpus", type=int, default=4)
    parser.add_argument("--cpu", default="max", help="QEMU CPU model and feature overrides")
    parser.add_argument("--accel", choices=("tcg", "kvm"), default="tcg")
    parser.add_argument("--apic", choices=("xapic", "x2apic"),
                        help="require a QEMU APIC mode and verify the boot handoff")
    parser.add_argument("--tlb", choices=("pcid-invpcid", "pcid", "invpcid", "cr3"),
                        help="require x86_64 TLB capabilities and verify the selected backend")
    parser.add_argument("--usb-debug", action="store_true",
                        help="build USB screen diagnostics and bounded HID report traces")
    parser.add_argument("--usb-irq", choices=("auto", "msix", "msi", "intx"), default="auto",
                        help="select the QEMU xHCI interrupt capabilities and verify the chosen mode")
    parser.add_argument("--usb-no-intx", action="store_true",
                        help="set both xHCI PCI Interrupt Line bytes to 255 before driver initialization")
    parser.add_argument("--usb-root-bus", type=lambda value: int(value, 0), default=0,
                        help="place USB behind an independent Q35 PCIe root (e.g. 0x80)")
    parser.add_argument("--usb-hubs", action="store_true",
                        help="attach keyboard/mouse through two hubs on a mixed USB2/USB3 controller")
    parser.add_argument("--ahci", action="store_true",
                        help="validate SATA reads, writes, flush and 48-bit capacity on two AHCI ports")
    parser.add_argument("--ahci-no-irq", action="store_true",
                        help="suppress AHCI command IRQs and verify bounded boot-time failure")
    parser.add_argument("--timeout", type=int, default=120)
    gui_mode = parser.add_mutually_exclusive_group()
    gui_mode.add_argument("--capacity", action="store_true", help="also cross the 255-task boundary")
    gui_mode.add_argument("--mouse", action="store_true", help="validate PS/2 motion, buttons and wheel using QMP")
    gui_mode.add_argument("--console", action="store_true", help="validate GUI shell rendering, input echo, backspace and scrolling")
    gui_mode.add_argument("--terminal-load", type=int, metavar="COUNT",
                          help="keep COUNT term processes open and verify a subsequent program launch")
    gui_mode.add_argument("--memory-pressure", action="store_true",
                          help="exhaust free pages, verify failed thread creation rolls back and programs recover")
    gui_mode.add_argument("--editor", action="store_true", help="edit and save files with pl_editor inside term using real keyboard events")
    gui_mode.add_argument("--sdl", action="store_true", help="validate SDL3 surfaces, renderer, image IO, fonts and input")
    gui_mode.add_argument("--desktop-app", choices=("lite", "nk"), help="capture and close an SDL desktop application")
    gui_mode.add_argument("--tools", action="store_true", help="run C4 pointer/VM, NASM object and JavaScript regressions")
    gui_mode.add_argument("--dynamic", action="store_true", help="run user ELF interpreter, shared libraries and page protection regressions")
    gui_mode.add_argument("--sched-balance", action="store_true", help="benchmark long/short runnable jobs and idle load balancing")
    gui_mode.add_argument("--sched-bench", type=int, metavar="SLEEPERS", help="measure scheduler handoff, yield and compute before/during/after parked threads")
    gui_mode.add_argument("--futex", action="store_true", help="validate native futex wait/wake, cancellation and concurrent allocation")
    gui_mode.add_argument("--threads", action="store_true", help="validate pthreads, ELF TLS, synchronization and thread resource release")
    gui_mode.add_argument("--llvm", action="store_true", help="validate native LLVM MCJIT, relocations, W^X and concurrent compilation")
    gui_mode.add_argument("--lavapipe", action="store_true", help="validate native Vulkan compute, SDL triangle pixels and window presentation")
    gui_mode.add_argument("--opengl", action="store_true", help="validate llvmpipe EGL/GLSL/contexts and classic glxgears pixels and input")
    gui_mode.add_argument("--cube", action="store_true", help="benchmark the Vulkan rotating cube and verify two frames")
    gui_mode.add_argument("--compute-bench", action="store_true", help="benchmark verified Vulkan TEA compute with 0, 1, 2 and 4 workers in both orders")
    parser.add_argument("--cube-workers", type=int, help="override cube LP_NUM_THREADS (0 runs without raster workers)")
    gui_mode.add_argument("--all-apps", action="store_true", help="validate and relocate every built application, then run dynamic regressions")
    gui_mode.add_argument("--usb", action="store_true", help="validate xHCI enumeration, USB speeds, hotplug and ring wrap")
    parser.add_argument("--out", type=Path, default=Path("/tmp/plant-x86_64-smoke"))
    parser.add_argument("--ovmf", type=Path, default=Path("/usr/share/OVMF"))
    args = parser.parse_args()
    if args.terminal_load is not None and args.terminal_load <= 0:
        parser.error("--terminal-load must be positive")
    if args.arch == "i386" and args.firmware == "uefi":
        parser.error("the i386 LiveCD uses BIOS")
    if args.arch == "i386" and args.tlb:
        parser.error("--tlb requires x86_64 paging")
    if (args.usb_no_intx or args.usb_irq != "auto" or args.usb_root_bus or args.usb_hubs) and not args.usb:
        parser.error("USB fixture options require --usb")
    if args.usb_no_intx and args.usb_irq == "intx":
        parser.error("--usb-no-intx requires MSI or MSI-X")
    if args.usb_root_bus and (args.machine != "q35" or not 1 <= args.usb_root_bus <= 253):
        parser.error("--usb-root-bus requires Q35 and a bus number from 1 to 253")
    if args.ahci_no_irq and not args.ahci:
        parser.error("--ahci-no-irq requires --ahci")
    repo = Path(__file__).resolve().parent.parent
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    native = args.arch == "x86_64"
    if args.cube_workers is not None and (not args.cube or not 0 <= args.cube_workers <= 2147483647):
        parser.error("--cube-workers requires --cube and a nonnegative signed 32-bit value")
    kernel_options = [f"USB_DEBUG={int(args.usb_debug)}"]
    commands = (["archtest.bin", "cpptest.bin", "simdtest.bin", "llvmtest.bin"] if native else ["fputest.bin"]) + [
        "timetest.bin", "thrdtest.bin", "libctest.bin", "cxxcheck.bin", "futest.bin", "exc_test.bin", "rpctest.bin", "dktest.bin", "dyntest.bin", "nettest.bin loopback",
        "guitest.bin capacity" if args.capacity else "guitest.bin mouse" if args.mouse else "guitest.bin",
        "psh.bin -c insmod hello.mod", "psh.bin -c rmmod hello_mod",
        "psh.bin -c insmod hello.mod", "psh.bin -c rmmod hello_mod",
        'lua.bin -e assert(math.sqrt(81)==9);assert(math.abs(math.sin(0.5)-0.479425538604203)<1e-12)',
    ]
    expected = (["ARCHTEST PASS", "CPPTEST PASS", "SIMDTEST PASS", "LLVMTEST PASS"] if native else ["FPUTEST PASS"]) + [
                "TIMETEST PASS", "THRDTEST PASS", "LIBCTEST PASS", "CXXCHECK PASS", "FUTEXTEST PASS", "EXCEPTION_TEST done checks=5 fails=0", "GUITEST THREAD PASS", "GMOUSE ID =",
                "RPCTEST done checks=23 fails=0", "DKTEST PASS", "DYNTEST PASS", "DYNTEST TLB PASS",
                "GUISTRESS PASS" if args.capacity else "GUITEST PASS"]
    if args.mouse:
        expected.append("GUIMOUSE PASS events=15")
    if args.console:
        commands = ["guitest.bin terminal"]
    if args.terminal_load:
        commands = [f"guitest.bin terminal-load {args.terminal_load}"]
        expected = ["TERMLOAD PASS", "TIMETEST PASS"]
    if args.memory_pressure:
        commands = ["guitest.bin memory-pressure"]
        expected = ["MEMORYPRESSURE PASS", "TIMETEST PASS"]
    if args.editor:
        commands = ["guitest.bin editor"]
        expected = ["EDITORTEST PASS", "GUITEST EDITOR PASS"]
    if args.sdl:
        commands = ["timetest.bin", "sdltest.bin"]
        expected = ["TIMETEST PASS", "SDLTEST PASS", "SDLFRAME PASS"]
    if args.desktop_app:
        commands = ["timetest.bin", f"sdltest.bin {args.desktop_app}.bin"]
        expected = ["TIMETEST PASS", f"SDLAPP EXIT {args.desktop_app}.bin status=0"]
        if args.desktop_app == "lite":
            expected.append("LITE EDIT PASS")
    if args.dynamic or args.all_apps:
        commands = ["dyntest.bin"]
        expected = ["DYNAMIC PASS", "DYNAMIC DATA PASS", "DYNTEST PASS", "DYNTEST TLB PASS", "DYNAMIC ATEXIT",
                    "DYNAMIC FINI main", "DYNAMIC FINI leaf", "DYNAMIC FINI base"]
        if args.all_apps:
            programs = subprocess.check_output(
                ["make", "--no-print-directory", "-s", "-C", str(repo / "apps"),
                 f"ARCH={args.arch}", "list-apps"], text=True).splitlines()
            commands = ["dyntest.bin --all"]
            expected.append(f"DYNAPPS PASS count={len(programs)}")
    if args.sched_balance:
        commands = ["schbench.bin balance"]
        expected = ["SCHEDBALANCE PASS", "SCHEDBALANCE repeat=7 "]
    if args.sched_bench is not None:
        if args.sched_bench < 1:
            parser.error("--sched-bench requires a positive sleeper count")
        commands = [f"schbench.bin {args.sched_bench}"]
        expected = ["SCHEDBENCH PASS"] + [
            f"SCHEDBENCH phase={phase} sleepers={args.sched_bench} repeat=7 "
            for phase in ("before", "parked", "after")]
    if args.futex:
        commands = ["futest.bin", "dyntest.bin"]
        expected = ["FUTEXTEST PASS", "DYNTEST PASS", "DYNTEST TLB PASS"]
    if args.threads:
        commands = ["thrdtest.bin", "libctest.bin", "cxxcheck.bin", "futest.bin", "timetest.bin", "dyntest.bin"]
        expected = ["THRDTEST PASS", "LIBCTEST PASS", "CXXCHECK PASS", "FUTEXTEST PASS", "TIMETEST PASS", "DYNTEST PASS"]
    if args.llvm:
        if not native:
            parser.error("--llvm currently requires x86_64")
        commands = ["llvmtest.bin"]
        expected = ["LLVMTEST PASS"]
    if args.lavapipe:
        if not native:
            parser.error("--lavapipe requires x86_64")
        commands = ["llvmtest.bin", "lvptest.bin --test"]
        expected = ["LLVMTEST PASS", "LVPCOMPUTE PASS", "LVPHEADLESS PASS", "LVPTEST PASS"]
    if args.opengl:
        if not native:
            parser.error("--opengl requires x86_64")
        commands = ["libctest.bin", "glxgears.bin --test"]
        expected = ["LIBCTEST PASS", "OPENGL TEST PASS", "GLXGEARS GL_RENDERER = llvmpipe", "GLXGEARS PASS"]
    if args.cube:
        if not native:
            parser.error("--cube requires x86_64")
        workers = "" if args.cube_workers is None else f" --workers {args.cube_workers}"
        commands = ["vkcube.bin --benchmark" + workers]
        expected = ["VKCUBE PASS"] + [f"VKCUBE BENCH round={i} " for i in range(3)]
    if args.compute_bench:
        if not native:
            parser.error("--compute-bench requires x86_64")
        commands = [f"lvptest.bin --benchmark --workers {workers}"
                    for workers in (0, 1, 2, 4, 4, 2, 1, 0)]
        expected = [f"LVPSCALE PASS workers={workers} " for workers in (0, 1, 2, 4)]
    if args.tools:
        # Commands and source files enter the guest through init.mst and Lua;
        # keyboard input is never used to execute commands.
        source = "int main(){int *p;p=malloc(16);*p=41;if(*p+1!=42)return 1;free(p);return 0;}"
        assembly = "section .text\nglobal probe\nprobe: mov eax,42\nret\n"
        def lua_bytes(value):
            return "string.char(" + ",".join(str(byte) for byte in value.encode()) + ")"
        prepare = ("f=assert(io.open([[/c4probe.c]],[[w]]));f:write(" + lua_bytes(source) + ");f:close();"
                   "f=assert(io.open([[/probe.asm]],[[w]]));f:write(" + lua_bytes(assembly) + ");f:close()")
        if not native:
            c_source = ('#include <stdlib.h>\n#include <stdio.h>\n'
                        '_Static_assert(sizeof(void*)==4,"ABI");\nint NowTaskID(void);\n'
                        'int main(void){void *p=malloc(32);if(!p)return 1;'
                        'free(p);printf("TCC runtime %d\\n",42);return NowTaskID()<1;}\n')
            c_prepare = ("f=assert(io.open([[/ccprobe.c]],[[w]]));f:write(" +
                         lua_bytes(c_source) + ");f:close()")
        bits = 64 if native else 32
        verify = ("f=assert(io.open([[/probe.o]],[[rb]]));h=f:read(20);f:close();"
                  f"assert(h:sub(1,4)==string.char(127)..[[ELF]]);assert(h:byte(5)=={2 if native else 1});"
                  f"assert(h:byte(19)=={62 if native else 3});"
                  "os.remove([[/probe.o]]);os.remove([[/probe.asm]]);os.remove([[/c4probe.c]])")
        commands = ["lua.bin -e " + prepare, "c4.bin /c4probe.c",
                    f"nasm.bin -f elf{bits} /probe.asm -o /probe.o",
                    "lua.bin -e " + verify,
                    "duktape.bin -e a=[];for(i=0;i<1000;i++)a.push({v:i});if(a[999].v!==999){throw(1);}"]
        expected = []
        if not native:
            commands += ["lua.bin -e " + c_prepare,
                         "tcc.bin /ccprobe.c -o /ccprobe.bin", "/ccprobe.bin",
                         "lua.bin -e os.remove([[/ccprobe.c]]);os.remove([[/ccprobe.bin]])"]
    if args.usb:
        commands = ["nettest.bin", "usbtest.bin", "guitest.bin usb"]
        expected = ["USBSTORAGE PASS", "USBKEY PASS", "GUIMOUSE PASS events=15", "GUITEST PASS"]
    if args.ahci:
        if args.machine != "q35" or args.usb:
            parser.error("--ahci requires --machine q35 and is run separately from --usb")
        sata_test = (
            'check=function(d,i)'
            'p=d..[[:/]];f=assert(io.open(p..[[ahcitag.txt]],[[rb]]));'
            'assert(f:read([[*a]])==tostring(i));assert(f:close());'
            's=string.rep(string.char(64+i),131072)..[[tail123]];'
            'f=assert(io.open(p..[[seed.bin]],[[rb]]));assert(f:read([[*a]])==s);assert(f:close());'
            'f=assert(io.open(p..[[written.bin]],[[w+]]));assert(f:write(s));'
            'assert(f:seek([[set]],513)==513);assert(f:write([[SATA]]));assert(f:flush());'
            'assert(f:seek([[set]],0)==0);'
            'assert(f:read([[*a]])==s:sub(1,513)..[[SATA]]..s:sub(518));assert(f:close());'
            'end;check([[C]],1);check([[D]],2)')
        commands = ["psh.bin -c remount_drive C:", "psh.bin -c remount_drive D:",
                    "lua.bin -e " + sata_test] + commands
        if args.ahci_no_irq:
            commands = []
            expected = ["ahci: port=0 command=ec completion=0", "command timeout or error",
                        "ahci: controllers=2 disks=0 initialization complete"]
    iso = repo / "kernel" / ("plant-os-x86_64.iso" if native else "plant-os-livecd.iso")
    init = repo / "kernel/res/init.mst"
    original = init.read_bytes()
    config = repo / "kernel/res/sys.cfg"
    original_config = config.read_bytes() if args.usb else None
    def mst_quote(text):
        return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'
    actions = [f'  {{"action" = "run" "command_line" = {mst_quote(command)}}}'
               for command in commands + ["psh.bin"]]
    with (output / "build.log").open("w") as build_log:
        try:
            if original_config is not None:
                configured, count = re.subn(rb'"network"\s*=\s*"[^"]*"',
                                           b'"network" = "enable"', original_config)
                config.write_bytes(configured if count else configured + b'\n"network" = "enable"\n')
            init.write_text('"todo" = [\n' + ',\n'.join(actions) + '\n]\n')
            subprocess.run(["make", "-C", str(repo / "apps"), f"ARCH={args.arch}",
                            "-j8"],
                           stdout=build_log, stderr=subprocess.STDOUT, check=True)
            if args.all_apps:
                app_output = repo / "apps/out" / ("x86_64" if native else "")
                for program in programs:
                    data = (app_output / program).read_bytes()
                    elf = subprocess.check_output(
                        ["readelf", "-lWd", str(app_output / program)], text=True)
                    if (data[:7] != b"\x7fELF" + bytes([2 if native else 1, 1, 1]) or
                            struct.unpack_from("<HH", data, 16) != (3, 62 if native else 3) or
                            "[Requesting program interpreter: /lib/ld.so]" not in elf or
                            not re.search(r"\(NEEDED\).*\[libp\.so\]", elf)):
                        raise RuntimeError(f"application is not a native dynamic PIE: {program}")
            if not native:
                subprocess.run(["make", "-C", str(repo / "loader")],
                               stdout=build_log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(["make", "-C", str(repo / "kernel"), f"ARCH={args.arch}",
                            *kernel_options, "livecd", "-j8"],
                           stdout=build_log, stderr=subprocess.STDOUT, check=True)
        finally:
            init.write_bytes(original)
            if original_config is not None:
                config.write_bytes(original_config)
    serial = output / "serial.log"
    serial.write_bytes(b"")
    cpu = args.cpu
    if args.apic:
        cpu += "," + ("+" if args.apic == "x2apic" else "-") + "x2apic,enforce"
    if args.tlb:
        pcid, invpcid = {"pcid-invpcid": (1, 1), "pcid": (1, 0),
                        "invpcid": (0, 1), "cr3": (0, 0)}[args.tlb]
        cpu += f",pcid={'on' if pcid else 'off'},invpcid={'on' if invpcid else 'off'},enforce"
    if args.cube or args.compute_bench or args.sched_bench is not None or args.sched_balance:
        configuration = {"arch": args.arch, "firmware": args.firmware,
                         "accel": args.accel, "cpu": cpu, "cpus": args.cpus,
                         "memory_mib": args.memory, "machine": args.machine,
                         "qemu": subprocess.check_output(["qemu-system-x86_64", "--version"], text=True).splitlines()[0]}
        (output / "configuration.json").write_text(json.dumps(configuration, indent=2) + "\n")
    command = ["qemu-system-x86_64", "-accel", args.accel, "-cpu", cpu, "-smp", str(args.cpus),
               "-machine", args.machine + (",i8042=off" if args.usb else ""),
               "-m", str(args.memory), "-rtc", "base=2026-09-05T04:05:06,clock=vm", "-cdrom", str(iso),
               "-boot", "d", "-display", "none", "-serial", f"file:{serial}",
               "-monitor", "none", "-no-reboot", "-no-shutdown", "-netdev", "user,id=net0",
               "-device", "pcnet,netdev=net0"]
    try:
        with tempfile.TemporaryDirectory(prefix="plant-ovmf-") as temporary:
            qmp_path = Path(temporary) / "qmp.sock"
            command += ["-qmp", f"unix:{qmp_path},server=on,wait=off"]
            gdb_path = Path(temporary) / "gdb.sock"
            qtest_path = Path(temporary) / "qtest.sock"
            if args.usb_no_intx or args.ahci_no_irq:
                command += ["-S", "-gdb", f"unix:{gdb_path},server=on,wait=off",
                            "-qtest", f"unix:{qtest_path},server=on,wait=off",
                            "-qtest-log", str(output / "qtest.log")]
            usb_images = []
            ahci_images = []
            if args.ahci:
                command += ["-device", "ich9-ahci,id=ahci-test"]
                sizes = (64 * 1024 * 1024,) if args.ahci_no_irq else (64 * 1024 * 1024, 160 * 1024 * 1024 * 1024)
                for index, size in enumerate(sizes):
                    disk = Path(temporary) / f"ahci-{index}.img"
                    with disk.open("wb") as stream:
                        stream.truncate(size)
                    subprocess.run(["mformat", "-i", str(disk), "-T", "131072", "::"], check=True)
                    tag = Path(temporary) / "ahcitag.txt"
                    tag.write_text(str(index + 1))
                    seed = Path(temporary) / "seed.bin"
                    payload = bytes([65 + index]) * 131072 + b"tail123"
                    seed.write_bytes(payload)
                    subprocess.run(["mcopy", "-i", str(disk), str(tag), "::/ahcitag.txt"], check=True)
                    subprocess.run(["mcopy", "-i", str(disk), str(seed), "::/seed.bin"], check=True)
                    command += ["-drive", f"id=sata{index},file={disk},format=raw,if=none",
                                "-device", f"ide-hd,drive=sata{index},bus=ahci-test.{index}"]
                    expected_bytes = payload[:513] + b"SATA" + payload[517:]
                    ahci_images.append((disk, expected_bytes))
            if args.usb:
                keyboard_bus = "usb3.0" if args.usb_hubs else "usb2.0"
                keyboard_port = "3.2.1" if args.usb_hubs else "1"
                mouse_port = "3.2.4" if args.usb_hubs else "4"
                capabilities = {"auto": "", "msix": ",msi=off,msix=on",
                                "msi": ",msi=on,msix=off", "intx": ",msi=off,msix=off"}[args.usb_irq]
                usb2_bus = usb3_bus = ""
                if args.usb_root_bus:
                    command += ["-device", f"pxb-pcie,id=usb-root,bus_nr={args.usb_root_bus}",
                                "-device", "pcie-root-port,id=usb-port2,bus=usb-root,chassis=1",
                                "-device", "pcie-root-port,id=usb-port3,bus=usb-root,chassis=2"]
                    usb2_bus, usb3_bus = ",bus=usb-port2", ",bus=usb-port3"
                command += ["-device", "qemu-xhci,id=usb2,p2=4,p3=0" + capabilities + usb2_bus,
                            "-device", f"qemu-xhci,id=usb3,p2={4 if args.usb_hubs else 0},p3=4" + capabilities + usb3_bus]
                if args.usb_hubs:
                    command += ["-device", "usb-hub,id=usb-hub-root,bus=usb3.0,port=3,port-power=on",
                                "-device", "usb-hub,id=usb-hub-child,bus=usb3.0,port=3.2,port-power=on"]
                command += ["-device", f"usb-kbd,id=usb-keyboard,bus={keyboard_bus},port={keyboard_port},usb_version=1",
                            "-device", f"usb-mouse,bus={keyboard_bus},port={mouse_port}",
                            "-audiodev", "none,id=usb-audio",
                            "-device", "usb-audio,audiodev=usb-audio,bus=usb2.0,port=3"]
                fixtures = (("high", "usb2.0", 2, 2048, 512),
                            ("super", "usb3.0", 1, 0, 512),
                            ("native4k", "usb3.0", 2, 0, 4096))
                for name, bus, port, first, block_size in fixtures:
                    disk = Path(temporary) / f"usb-{name}.img"
                    sectors = 131072
                    with disk.open("wb") as stream:
                        stream.truncate(sectors * 512)
                        if first:
                            mbr = bytearray(512)
                            mbr[450] = 0x0c
                            struct.pack_into("<II", mbr, 454, first, sectors - first)
                            mbr[510:512] = b"\x55\xaa"
                            stream.seek(0)
                            stream.write(mbr)
                    image = f"{disk}@@{first * 512}"
                    subprocess.run(["mformat", "-i", image, "-T", str(sectors - first),
                                    *(["-F"] if first else []), "::"], check=True)
                    tag = Path(temporary) / "usbtag.txt"
                    tag.write_text(name)
                    subprocess.run(["mcopy", "-i", image, str(tag), "::/usbtag.txt"], check=True)
                    usb_images.append((name, image))
                    command += ["-blockdev", json.dumps({"driver": "raw", "node-name": f"usb-{name}",
                                "file": {"driver": "file", "filename": str(disk)}}),
                                "-device", f"usb-storage,id=device-{name},bus={bus},port={port},"
                                           f"drive=usb-{name},logical_block_size={block_size},physical_block_size={block_size}"]
            if args.firmware == "uefi":
                variables = Path(temporary) / "vars.fd"
                shutil.copyfile(args.ovmf / "OVMF_VARS_4M.fd", variables)
                command += ["-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf / 'OVMF_CODE_4M.fd'}",
                            "-drive", f"if=pflash,format=raw,file={variables}"]
            with (output / "qemu.log").open("w") as qemu_log:
                guest = subprocess.Popen(command, stdout=qemu_log, stderr=subprocess.STDOUT)
                try:
                    if args.usb_no_intx or args.ahci_no_irq:
                        kernel = repo / "kernel/obj" / ("x86_64/kernel.bin" if native else "kernel.bin")
                        prepare_pci_fixture(qmp_path, gdb_path, qtest_path, kernel, args.ahci_no_irq)
                    deadline = time.monotonic() + args.timeout
                    mouse_sent = False
                    frames_checked = set()
                    app_started = None
                    app_closed = False
                    usb_steps = set()
                    while time.monotonic() < deadline and guest.poll() is None:
                        text = serial.read_text(errors="replace") if serial.exists() else ""
                        if re.search(r"PANIC|x86_64 exception .*cs=8", text):
                            raise RuntimeError(f"kernel failure; see {serial}")
                        if args.terminal_load and ("TERMLOAD FAIL" in text or "TTY RPC disconnected" in text):
                            raise RuntimeError(f"terminal load failed; see {serial}")
                        if args.terminal_load and "TERMLOAD PASS" in text:
                            exercise_terminal_load(qmp_path, output)
                            print(f"{args.arch} TERMLOAD PASS: {args.terminal_load} terminals, input, pixels, program launch; {serial}")
                            return
                        if args.tlb and "init.bin started" in text:
                            if f"tlb: pcid={pcid} invpcid={invpcid}\n" not in text:
                                raise RuntimeError(f"TLB backend mismatch; see {serial}")
                        if args.apic and "init.bin started" in text:
                            markers = [f"apic: enabled {args.apic} "]
                            if native:
                                markers.append(f"smp: Limine handoff mode={args.apic} cpus={args.cpus} ")
                            if any(marker not in text for marker in markers):
                                raise RuntimeError(f"APIC mode/handoff mismatch; see {serial}")
                        if args.machine == "q35" and "init.bin started" in text:
                            if not re.search(r"pci: segment=0000 bus=00 access=ECAM", text):
                                raise RuntimeError(f"Q35 did not use MCFG/ECAM; see {serial}")
                        if args.usb:
                            if re.search(r"usb-hub: .*failed|usb: interface .*class=09 unavailable", text):
                                raise RuntimeError(f"hub enumeration failure; see {serial}")
                            for marker in ("USBSTORAGE REMOVE_READY", "USBSTORAGE REINSERT_READY",
                                           "USBKEY READY", "USBKEY REPEAT_READY", "USBKEY HOLD_READY",
                                           "USBKEY REMOVE_READY", "USBKEY PASS"):
                                if marker not in text or marker in usb_steps:
                                    continue
                                qmp = QMP(qmp_path)
                                try:
                                    if marker == "USBSTORAGE REMOVE_READY":
                                        qmp.execute("device_del", id="device-high")
                                    elif marker == "USBSTORAGE REINSERT_READY":
                                        qmp.execute("device_add", driver="usb-storage", id="device-high",
                                                    bus="usb2.0", port="2", drive="usb-high")
                                    elif marker == "USBKEY READY":
                                        qmp.press("key", key={"type": "qcode", "data": "a"})
                                        qmp.chord("shift", "b")
                                        qmp.press("key", key={"type": "qcode", "data": "left"})
                                        qmp.press("key", key={"type": "qcode", "data": "backspace"})
                                    elif marker == "USBKEY REPEAT_READY":
                                        for down in (True, False):
                                            qmp.execute("input-send-event", events=[
                                                {"type": "key", "data": {"down": down,
                                                 "key": {"type": "qcode", "data": "c"}}}])
                                            if down: time.sleep(0.8)
                                    elif marker == "USBKEY HOLD_READY":
                                        qmp.execute("input-send-event", events=[
                                            {"type": "key", "data": {"down": True,
                                             "key": {"type": "qcode", "data": "shift"}}}])
                                    elif marker == "USBKEY REMOVE_READY":
                                        qmp.execute("device_del", id="usb-keyboard")
                                    else:
                                        qmp.execute("device_add", driver="usb-kbd", id="usb-keyboard",
                                                    bus=keyboard_bus, port=keyboard_port, usb_version=1)
                                        time.sleep(0.5)
                                        qmp.execute("input-send-event", events=[
                                            {"type": "key", "data": {"down": False,
                                             "key": {"type": "qcode", "data": "shift"}}}])
                                    usb_steps.add(marker)
                                finally:
                                    qmp.close()
                        if args.editor and "EDITORTEST START phase=1" in text and not mouse_sent:
                            exercise_editor(qmp_path, serial, output)
                            mouse_sent = True
                        if args.console and "GUITERM PASS" in text:
                            exercise_console(qmp_path, output)
                            print(f"{args.arch} {args.firmware} GUICONSOLE PASS: prompt, echo, backspace, scroll, close/reopen; {output}")
                            return
                        if args.desktop_app and f"SDLAPP START {args.desktop_app}.bin" in text:
                            if app_started is None:
                                app_started = time.monotonic()
                            if not app_closed and time.monotonic() - app_started > 10:
                                qmp = QMP(qmp_path)
                                try:
                                    if args.desktop_app == "lite":
                                        qmp.chord("ctrl", "end")
                                        qmp.press("key", key={"type": "qcode", "data": "a"})
                                        qmp.chord("ctrl", "s")
                                    width, height, pixels = qmp.screenshot(output / f"{args.desktop_app}.ppm")
                                    # The client must have drawn content inside its window.
                                    sample = b"".join(pixels[(y * width + 120) * 3:(y * width + 800) * 3]
                                                      for y in range(120, 650))
                                    if len(set(sample[i:i + 3] for i in range(0, len(sample), 3))) < 8:
                                        raise RuntimeError("desktop application has no rendered content")
                                    # SDL centers undefined/centered coordinates in the desktop.
                                    app_width = int(width * 0.8) if args.desktop_app == "lite" else min(1000, width - 16)
                                    app_height = int(height * 0.8) if args.desktop_app == "lite" else min(800, height - 48)
                                    left, top = (width - app_width) // 2, (height - app_height) // 2
                                    if args.desktop_app == "nk":
                                        probes = [(left + 100, top + 120, (255, 0, 0)),
                                                  (left + 100, top + 370, (0, 255, 0)),
                                                  (left + 460, top + 210, (0, 0, 255))]
                                        flicker = 0
                                        for frame in range(40):
                                            width, height, pixels = qmp.screenshot(output / "nk-refresh.ppm")
                                            if any(tuple(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) != color
                                                   for x, y, color in probes):
                                                flicker += 1
                                            time.sleep(0.05)
                                        (output / "nk-refresh.json").write_text(json.dumps({"samples": 40, "incomplete_frames": flicker}) + "\n")
                                        if flicker:
                                            raise RuntimeError(f"nk exposed incomplete content in {flicker}/40 samples")
                                    x, y = left + app_width - 7, top + 10
                                    qmp.move(x - width // 2, y - height // 2)
                                    qmp.press("btn", button="left")
                                    app_closed = True
                                finally:
                                    qmp.close()
                        sdl = re.search(r"SDLTEST READY origin=(\d+),(\d+) target=(\d+),(\d+)", text)
                        if args.sdl and sdl and not mouse_sent:
                            qmp = QMP(qmp_path)
                            try:
                                width, height, pixels = qmp.screenshot(output / "sdl-surfaces.ppm")
                                for x, y, color in ((100, 210, b"\x18\x40\x80"), (440, 160, b"\x20\xc0\x40")):
                                    offset = (y * width + x) * 3
                                    if pixels[offset:offset + 3] != color:
                                        raise RuntimeError(f"SDL framebuffer mismatch at {x},{y}")
                                qmp.move(int(sdl[3]) - int(sdl[1]), int(sdl[4]) - int(sdl[2]))
                                qmp.press("btn", button="left")
                                qmp.press("btn", button="wheel-up")
                                qmp.press("key", key={"type": "qcode", "data": "a"})
                                qmp.chord("shift", "a")
                                qmp.press("key", key={"type": "qcode", "data": "left"})
                                mouse_sent = True
                            finally:
                                qmp.close()
                        for phase in re.findall(r"^SDLFRAME READY (\w+)$", text, re.M):
                            if not args.sdl or phase in frames_checked:
                                continue
                            expected_pixels = {
                                "draft": [(440, 160, (32, 192, 64))],
                                "partial": [(100, 200, (240, 160, 32)), (160, 210, (24, 64, 128))],
                                "presented": [(440, 160, (224, 48, 32))],
                            }[phase]
                            qmp = QMP(qmp_path)
                            try:
                                width, height, pixels = qmp.screenshot(output / f"sdl-frame-{phase}.ppm")
                                for x, y, color in expected_pixels:
                                    offset = (y * width + x) * 3
                                    actual = tuple(pixels[offset:offset + 3])
                                    if actual != color:
                                        raise RuntimeError(f"SDL {phase} exposed incorrect pixels at {x},{y}: {actual} != {color}")
                                qmp.press("key", key={"type": "qcode", "data": "spc"})
                                frames_checked.add(phase)
                            finally:
                                qmp.close()
                        for phase, left, top in re.findall(r"LVPFRAME READY phase=(\d+) x=(-?\d+) y=(-?\d+)", text):
                            if not args.lavapipe or phase in frames_checked:
                                continue
                            qmp = QMP(qmp_path)
                            try:
                                width, height, pixels = qmp.screenshot(output / f"lavapipe-{phase}.ppm")
                                origin_x, origin_y = int(left) + 4, int(top) + 24
                                samples = [(8, 8), (65, 190), (255, 190), (160, 70), (160, 140)]
                                colors = []
                                for x, y in samples:
                                    x, y = x + origin_x, y + origin_y
                                    if not (0 <= x < width and 0 <= y < height):
                                        raise RuntimeError("lavapipe window is outside the framebuffer")
                                    offset = (y * width + x) * 3
                                    colors.append(tuple(pixels[offset:offset + 3]))
                                background = (16, 32, 48) if phase == "0" else (48, 32, 16)
                                if colors[0] != background:
                                    raise RuntimeError(f"lavapipe background: {colors[0]} != {background}")
                                for color, dominant in zip(colors[1:4], (0, 1, 2)):
                                    if color[dominant] <= 180 or any(color[i] >= 60 for i in range(3) if i != dominant):
                                        raise RuntimeError(f"lavapipe triangle has incorrect interpolation: {colors}")
                                if min(colors[4]) <= 50:
                                    raise RuntimeError(f"lavapipe triangle center: {colors[4]}")
                                qmp.press("key", key={"type": "qcode", "data": "spc"})
                                frames_checked.add(phase)
                            finally:
                                qmp.close()
                        for phase, left, top, client_width, client_height, checksum in re.findall(
                                r"GLXGEARS FRAME phase=(\d+) x=(-?\d+) y=(-?\d+) width=(\d+) height=(\d+) hash=([0-9a-f]+)", text):
                            if not args.opengl or phase in frames_checked:
                                continue
                            terminal = re.search(r"TERM ready x=(-?\d+) y=(-?\d+) width=(\d+) height=(\d+)", text)
                            if phase == "0" and not terminal:
                                continue
                            qmp = QMP(qmp_path)
                            try:
                                width, height, pixels = qmp.screenshot(output / f"glxgears-{phase}.ppm")
                                if phase == "0":
                                    # The cold-start GUI opens a terminal asynchronously.
                                    # Close that fixture window before checking the gears.
                                    tx, ty, tw, _ = map(int, terminal.groups())
                                    qmp.move(tx + tw - 13 - width // 2, ty + 15 - height // 2)
                                    qmp.press("btn", button="left")
                                    qmp.move(-width, -height)
                                    width, height, pixels = qmp.screenshot(output / f"glxgears-{phase}.ppm")
                                x, y = int(left) + 4, int(top) + 24
                                w, h = int(client_width), int(client_height)
                                if x < 0 or y < 0 or x + w > width or y + h > height:
                                    raise RuntimeError("glxgears window is outside the framebuffer")
                                client = b"".join(pixels[((y + row) * width + x) * 3:
                                                        ((y + row) * width + x + w) * 3]
                                                  for row in range(h))
                                actual = 2166136261
                                for byte in client:
                                    actual = ((actual ^ byte) * 16777619) & 0xffffffff
                                if actual != int(checksum, 16):
                                    raise RuntimeError(f"glxgears presented pixels differ from GL readback: {actual:08x} != {checksum}")
                                colors = Counter(client[i:i + 3] for i in range(0, len(client), 3))
                                for channel in range(3):
                                    count = sum(n for c, n in colors.items()
                                                if c[channel] > 80 and all(c[channel] > 2 * c[i]
                                                                         for i in range(3) if i != channel))
                                    if count < w * h // 100:
                                        raise RuntimeError(f"glxgears missing gear color {channel}: {count} pixels")
                                if colors[bytes(3)] < w * h // 4:
                                    raise RuntimeError("glxgears missing black background")
                                (output / f"glxgears-{phase}.rgb").write_bytes(client)
                                if phase == "1" and client == (output / "glxgears-0.rgb").read_bytes():
                                    raise RuntimeError("glxgears did not rotate after keyboard input")
                                qmp.press("key", key={"type": "qcode", "data": "right" if phase == "0" else "esc"})
                                frames_checked.add(phase)
                            finally:
                                qmp.close()
                        for phase, left, top in re.findall(r"VKCUBE FRAME phase=(\d+) x=(-?\d+) y=(-?\d+)", text):
                            if not args.cube or phase in frames_checked:
                                continue
                            qmp = QMP(qmp_path)
                            try:
                                width, height, pixels = qmp.screenshot(output / f"cube-{phase}.ppm")
                                x, y = int(left) + 4, int(top) + 24
                                if x < 0 or y < 0 or x + 640 > width or y + 480 > height:
                                    raise RuntimeError("cube window is outside the framebuffer")
                                client = b"".join(pixels[((y + row) * width + x) * 3:
                                                        ((y + row) * width + x + 640) * 3]
                                                  for row in range(480))
                                colors = Counter(client[i:i + 3] for i in range(0, len(client), 3))
                                background = bytes((16, 24, 40))
                                if colors[background] < 100000 or sum(n > 1000 for c, n in colors.items() if c != background) < 3:
                                    raise RuntimeError(f"cube missing background or three visible faces: {colors.most_common(8)}")
                                (output / f"cube-{phase}.rgb").write_bytes(client)
                                if phase == "1" and client == (output / "cube-0.rgb").read_bytes():
                                    raise RuntimeError("cube did not rotate")
                                qmp.press("key", key={"type": "qcode", "data": "spc"})
                                frames_checked.add(phase)
                            finally:
                                qmp.close()
                        mouse = re.search(r"GUIMOUSE READY origin=(\d+),(\d+) target=(\d+),(\d+)", text)
                        if (args.mouse or args.usb) and mouse and not mouse_sent:
                            origin = tuple(map(int, mouse.group(1, 2)))
                            target = tuple(map(int, mouse.group(3, 4)))
                            exercise_mouse(qmp_path, origin, target, output)
                            mouse_sent = True
                        if "init: run psh.bin\n" in text:
                            statuses = re.findall(r"^init: command .* status=(-?\d+)$", text, re.M)
                            if len(statuses) != len(commands) or any(status != "0" for status in statuses):
                                qmp = QMP(qmp_path)
                                try:
                                    qmp.screenshot(output / "failure.ppm")
                                finally:
                                    qmp.close()
                                raise RuntimeError(f"command regression; see {serial}")
                            missing = [marker for marker in expected if marker not in text]
                            if missing:
                                raise RuntimeError(f"missing {missing}; see {serial}")
                            if args.compute_bench:
                                subprocess.run(["python3", str(repo / "scripts/compare-compute.py"),
                                                str(output), "--out", str(output / "results.json")], check=True)
                            if "dyntest.bin" in commands:
                                markers = ["DYNAMIC PASS", "DYNAMIC ATEXIT", "DYNAMIC FINI main",
                                           "DYNAMIC FINI leaf", "DYNAMIC FINI base", "DYNTEST PASS"]
                                positions = [text.index(marker) for marker in markers]
                                if positions != sorted(positions) or any(text.count(marker) != 1 for marker in markers):
                                    raise RuntimeError(f"dynamic initialization/finalization order mismatch; see {serial}")
                            if args.usb:
                                modes = re.findall(r"^xhci: (\S+) .* transport=(\w+) ready$", text, re.M)
                                expected_mode = "msix" if args.usb_irq == "auto" else args.usb_irq
                                if len(modes) != 2 or any(mode != expected_mode for _, mode in modes):
                                    raise RuntimeError(f"USB interrupt mode mismatch: {modes}")
                                if args.usb_root_bus:
                                    if any(int(bdf.split(":")[-2], 16) <= args.usb_root_bus for bdf, _ in modes):
                                        raise RuntimeError(f"USB controllers are not below the independent root: {modes}")
                                    marker = f"pci: ECAM discovery entry segment=0000 bus={args.usb_root_bus:02x} "
                                    if marker not in text:
                                        raise RuntimeError("independent PCI root was not discovered")
                                if args.usb_no_intx and len(re.findall(r"^usb: PCI .*prog-if=30 irq=255$", text, re.M)) != 2:
                                    raise RuntimeError("xHCI did not observe both missing INTx routes")
                                exercise_console(qmp_path, output, (128, 128))
                                print("USBCONSOLE PASS: shell echo, backspace, scrolling", flush=True)
                                exercise_usb(qmp_path, serial, guest, deadline,
                                             keyboard_bus, keyboard_port, args.usb_hubs)
                                # Read from the backing images after guest exit, independently
                                # of its page cache and USB driver.
                                guest.terminate()
                                guest.wait(timeout=5)
                                for name, image in usb_images:
                                    destination = output / f"usb-{name}-data.bin"
                                    subprocess.run(["mcopy", "-o", "-i", image, "::/usbdata.bin",
                                                    str(destination)], check=True)
                                    expected_bytes = bytearray(((i * 37) ^ (i >> 8) ^ 0x5a) & 255
                                                               for i in range(128 * 1024))
                                    expected_bytes[513:516] = b"USB"
                                    if destination.read_bytes() != expected_bytes:
                                        raise RuntimeError(f"USB persistent write mismatch: {name}")
                                print("USB persistence PASS: host verified all three backing images", flush=True)
                            if args.ahci and not args.ahci_no_irq:
                                if "sectors=335544320 lba=48 ready" not in text:
                                    raise RuntimeError("AHCI did not preserve the large disk's 48-bit capacity")
                                guest.terminate()
                                guest.wait(timeout=5)
                                for index, (disk, expected_bytes) in enumerate(ahci_images):
                                    destination = output / f"ahci-{index}-written.bin"
                                    subprocess.run(["mcopy", "-o", "-i", str(disk), "::/written.bin", str(destination)], check=True)
                                    if destination.read_bytes() != expected_bytes:
                                        raise RuntimeError(f"AHCI backing image mismatch: {index}")
                                print("AHCI persistence PASS: two ports, read/write/flush, unaligned write, 48-bit capacity", flush=True)
                            elif args.ahci_no_irq:
                                print("AHCI timeout PASS: lost IRQ did not stall startup", flush=True)
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
                                       "ARCH=i386", *kernel_options, "livecd", "-j8"])
            subprocess.run(restore,
                           stdout=restore_log, stderr=subprocess.STDOUT, check=True)


if __name__ == "__main__":
    main()
