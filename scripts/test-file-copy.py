#!/usr/bin/env python3
"""Copy a large file within FAT32 and check contents and disk write amplification."""
import argparse
import importlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from sources import ROOT, Sources, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="default: pinned Minecraft server JAR")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--accel", choices=("kvm", "tcg"), default="kvm")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--flush-error", action="store_true",
                        help="inject a disk flush failure; require copy and shutdown to fail")
    args = parser.parse_args()
    source = args.source.resolve() if args.source else Sources(ROOT / "apps/minecraft")["server"].prepare()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    qmp_type = importlib.import_module("test-x86_64").QMP
    with tempfile.TemporaryDirectory(prefix="copy-", dir=output) as temporary:
        work = Path(temporary)
        disk, iso = work / "disk.img", work / "test.iso"
        with disk.open("wb") as stream:
            stream.truncate(max(1536 * 1024 * 1024, source.stat().st_size * 3))
        subprocess.run(["mformat", "-i", str(disk), "-F", "::"], check=True)
        subprocess.run(["mcopy", "-i", str(disk), str(source), "::/source.bin"], check=True)
        launcher = work / "copy-test.lua"
        launcher.write_text('local start = os.clock()\n'
                            'assert(os.execute("copy.bin C:/source.bin C:/destination.bin"))\n'
                            'local elapsed = os.clock() - start\n'
                            'local result = assert(io.open("C:/copy-time.txt", "w"))\n'
                            'assert(result:write(string.format("%.3f\\n", elapsed)))\n'
                            'assert(result:close())\n')
        subprocess.run(["mcopy", "-i", str(disk), str(launcher), "::/copy-test.lua"], check=True)
        init = work / "init.mst"
        command = "lua.bin C:/copy-test.lua"
        init.write_text('"todo" = [\n'
                        '{"action" = "run" "command_line" = "' + command + '"},\n'
                        '{"action" = "run" "command_line" = "psh.bin -c shutdown"}\n]\n')
        with (output / "build.log").open("w") as log:
            subprocess.run(["make", "-C", str(ROOT / "kernel"), "ARCH=x86_64", "full", "-j4"],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run([str(ROOT / "scripts/build-livecd.sh"), str(iso), "x86_64"],
                           env=dict(os.environ, PLANT_INIT_SCRIPT=str(init)),
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        serial, socket = output / "serial.log", work / "qmp.sock"
        serial.write_text("")
        cpu = "host" if args.accel == "kvm" else "max,-xgetbv1"
        backend = str(disk)
        if args.flush_error:
            fault = work / "blkdebug.conf"
            fault.write_text('[inject-error]\nevent = "flush_to_disk"\n'
                             'errno = "5"\nonce = "on"\nimmediately = "on"\n')
            backend = f"blkdebug:{fault}:{disk}"
        with (output / "qemu.log").open("w") as log:
            guest = subprocess.Popen([
                "qemu-system-x86_64", "-accel", args.accel, "-cpu", cpu,
                "-m", "2048", "-smp", "4", "-display", "none", "-monitor", "none",
                "-no-reboot", "-no-shutdown", "-serial", f"file:{serial}",
                "-qmp", f"unix:{socket},server=on,wait=off", "-cdrom", str(iso), "-boot", "d",
                "-drive", f"file={backend},format=raw,if=ide,index=0,werror=report"],
                stdout=log, stderr=subprocess.STDOUT)
            qmp = None
            started = time.monotonic()
            try:
                while guest.poll() is None and time.monotonic() - started < args.timeout:
                    if qmp is None:
                        try:
                            qmp = qmp_type(socket)
                        except (OSError, TimeoutError):
                            time.sleep(0.1)
                            continue
                    text = serial.read_text(errors="replace")
                    if (args.flush_error and "acpi: disk C sync failed" in text and
                            re.search(re.escape(f"init: command {command} status=") + r"-?[1-9]\d*", text)):
                        break
                    if "acpi: entering S5" in text:
                        if qmp.execute("query-status")["status"] == "shutdown":
                            break
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f"file copy did not finish; see {serial}")
                elapsed = time.monotonic() - started
                stats = next(item["stats"] for item in qmp.execute("query-blockstats")
                             if item.get("device") == "ide0-hd0")
            finally:
                if qmp is not None:
                    qmp.execute("quit")
                    qmp.close()
                if guest.poll() is None:
                    try:
                        guest.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        guest.kill()
                        guest.wait()
        if args.flush_error:
            if "acpi: entering S5" in serial.read_text(errors="replace") or not stats["failed_flush_operations"]:
                raise RuntimeError("flush failure was not propagated")
            (output / "result.json").write_text(json.dumps(stats, indent=2) + "\n")
            print(f"IDE FLUSH FAILURE: COPY ERROR AND SHUTDOWN REFUSAL PASS: {output}")
            return
        if f"init: command {command} status=0" not in serial.read_text(errors="replace"):
            raise RuntimeError("copy command failed")
        checksum = digest(source)
        for name in ("source", "destination"):
            target = work / name
            subprocess.run(["mcopy", "-i", str(disk), f"::/{name}.bin", str(target)], check=True)
            if digest(target) != checksum:
                raise RuntimeError(f"{name} content mismatch")
        copy_seconds = float(subprocess.check_output(
            ["mtype", "-i", str(disk), "::/copy-time.txt"], text=True))
        result = {"source_bytes": source.stat().st_size, "sha256": checksum,
                  "copy_seconds": copy_seconds,
                  "boot_and_copy_seconds": elapsed, "disk": stats}
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        # Allow metadata overhead, but reject redundant full-file zeroing and
        # per-cluster writes independently of the host's storage speed.
        if (stats["wr_bytes"] > source.stat().st_size * 2 + 16 * 1024 * 1024 or
                stats["wr_operations"] > source.stat().st_size // (16 * 1024) + 256):
            raise RuntimeError("excessive FAT write amplification")
        if not 2 <= stats["flush_operations"] <= 4:
            raise RuntimeError("copy fsync/shutdown did not use bounded explicit disk flushes")
        print(f"FILE COPY CONTENT AND WRITE AMPLIFICATION PASS: copy={copy_seconds:.3f}s, "
              f"boot+copy={elapsed:.2f}s; {output}")


if __name__ == "__main__":
    main()
