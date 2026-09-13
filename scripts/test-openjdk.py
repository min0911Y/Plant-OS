#!/usr/bin/env python3
"""Run native OpenJDK source/class launch, runtime, NIO and C1/C2 regressions."""
import argparse
from collections import Counter
import importlib
import os
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jdk", type=Path, required=True, help="Plant JDK image")
    parser.add_argument("--javac", required=True, help="host JDK 17+ javac")
    parser.add_argument("--out", type=Path, default=Path("/tmp/plant-openjdk-nio"))
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--memory", type=int, default=2048, help="guest RAM in MiB")
    parser.add_argument("--accel", choices=("tcg", "kvm"), default="tcg")
    parser.add_argument("--tcg-thread", choices=("single", "multi"), default="multi",
                        help="TCG host threading (default: multi)")
    parser.add_argument("--jit", choices=("c1", "c2"),
                        help="also require a correct workload and a compiled HotSpot nmethod")
    parser.add_argument("--repeat", type=int, default=1,
                        help="repeat source/class, runtime, NIO and JIT commands in the same boot")
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be positive")
    repo = Path(__file__).resolve().parents[1]
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    classes = output / "classes"
    classes.mkdir(exist_ok=True)
    subprocess.run([args.javac, "--release", "17", "-d", str(classes),
                    str(repo / "apps/openjdk/Startup.java"),
                    str(repo / "apps/openjdk/Nio.java"),
                    str(repo / "apps/openjdk/RuntimeChecks.java"),
                    str(repo / "apps/openjdk/Jit.java")], check=True)
    launcher = output / "launcher.lua"
    launcher.write_text('local times = {}\n'
                        'for i = 1, 3 do\n'
                        '  local start = os.clock()\n'
                        '  assert(os.execute(i == 1 and "C:/java/bin/java --version" or "java --version"))\n'
                        '  times[i] = string.format("%.3f", os.clock() - start)\n'
                        '  if i == 1 then assert(os.execute("cd C:/java/bin")) end\n'
                        'end\n'
                        'local file = assert(io.open("C:/java/startup-times.txt", "w"))\n'
                        'assert(file:write(table.concat(times, "\\n"), "\\n"))\n'
                        'assert(file:close())\n'
                        'assert(os.execute("cd C:/"))\n'
                        'assert(os.execute("java/bin/java java/Hello.java C:/java/relative-result.txt"))\n')
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
                'lua.bin C:/java/launcher.lua',
                'C:/java/bin/java -XX:ErrorFile=C:/java/hs_err.log C:/java/Hello.java C:/java/hello-result.txt',
                'C:/java/bin/javac -J-XX:ErrorFile=C:/java/javac-hs_err.log -d C:/java C:/java/Hello.java',
                'C:/java/bin/java -XX:ErrorFile=C:/java/class-hs_err.log -cp C:/java Hello C:/java/class-result.txt',
                'C:/java/bin/java -Xms16m -Xmx128m -XX:ErrorFile=C:/java/hs_err.log -Djava.net.preferIPv4Stack=true -cp C:/missing;C:/java Nio C:/java/nio-result.txt',
                'C:/java/bin/java -Xms16m -Xmx32m -XX:ErrorFile=C:/java/runtime-hs_err.log -cp C:/java RuntimeChecks C:/java/runtime-result.txt',
            ]
            if args.jit:
                commands[1] = commands[1].replace(
                    ' -XX:ErrorFile=', ' -XX:+UnlockDiagnosticVMOptions -XX:+LogCompilation '
                    '-XX:LogFile=C:/java/source-jit.xml -XX:ErrorFile=', 1)
                compiler = ('-XX:+TieredCompilation -XX:TieredStopAtLevel=1' if args.jit == 'c1'
                            else '-XX:-TieredCompilation')
                commands.append(
                    'C:/java/bin/java -Xms16m -Xmx128m -XX:ErrorFile=C:/java/hs_err.log -Xbatch -XX:CompileThreshold=100 '
                    '-XX:+UnlockDiagnosticVMOptions -XX:+LogCompilation -XX:LogFile=C:/java/jit.xml '
                    '-XX:CompileCommand=compileonly,Jit::kernel '
                    f'{compiler} -cp C:/java Jit C:/java/jit-result.txt')
            commands = commands[:1] + commands[1:] * args.repeat + ['psh.bin -c shutdown']
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
    for path in [launcher, repo / "apps/openjdk/Hello.java", *classes.glob("*.class")]:
        subprocess.run(["mcopy", "-o", "-i", str(disk), str(path), "::/java/"], check=True)
    # The result must be produced by this boot, even if the supplied JDK image
    # happens to contain an earlier test's output.
    for name in ("Hello.class", "hello-result.txt", "relative-result.txt", "class-result.txt",
                 "javac-hs_err.log", "class-hs_err.log", "runtime-result.txt", "runtime-hs_err.log",
                 "nio-result.txt", "startup-times.txt", "jit-result.txt", "jit.xml",
                 "source-jit.xml", "hs_err.log"):
        subprocess.run(["mdel", "-i", str(disk), f"::/java/{name}"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    serial = output / "serial.log"
    accelerator = f"tcg,thread={args.tcg_thread}" if args.accel == "tcg" else args.accel
    command = ["qemu-system-x86_64", "-accel", accelerator, "-cpu",
               "host" if args.accel == "kvm" else "max,-xgetbv1", "-smp", "4",
               "-m", str(args.memory), "-display", "none", "-monitor", "none", "-no-reboot", "-no-shutdown",
               "-qmp", f"unix:{output / 'qmp.sock'},server=on,wait=off",
               "-serial", f"file:{serial}", "-cdrom", str(iso), "-boot", "d",
               "-drive", f"file={disk},format=raw,if=ide,index=0"]
    with (output / "qemu.log").open("w") as log:
        guest = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        qmp = None
        screenshot_at = time.monotonic()
        deadline = time.monotonic() + args.timeout
        try:
            while guest.poll() is None and time.monotonic() < deadline:
                if qmp is None:
                    try:
                        qmp = importlib.import_module("test-x86_64").QMP(output / "qmp.sock")
                    except (FileNotFoundError, ConnectionRefusedError):
                        time.sleep(0.1)
                        continue
                if time.monotonic() >= screenshot_at:
                    qmp.screenshot(output / "console.ppm")
                    screenshot_at = time.monotonic() + 10
                if qmp.execute("query-status")["status"] == "shutdown":
                    qmp.screenshot(output / "console.ppm")
                    qmp.execute("quit")
                    guest.wait(timeout=10)
                    break
                time.sleep(0.1)
            else:
                if qmp is not None and guest.poll() is None:
                    qmp.screenshot(output / "console.ppm")
                raise RuntimeError(f"OpenJDK guest did not shut down; see {output}")
            if guest.returncode != 0:
                raise RuntimeError(f"QEMU failed with status {guest.returncode}; see {output}")
        finally:
            if qmp is not None:
                qmp.close()
            if guest.poll() is None:
                guest.kill()
            guest.wait()
    text = serial.read_text(errors="replace")
    successful = Counter(text.splitlines())
    expected = Counter(f"init: command {command} status=0" for command in commands[:-1])
    if "acpi: entering S5" not in text or any(
            successful[line] != count for line, count in expected.items()):
        raise RuntimeError(f"OpenJDK command failed or shutdown was incomplete; see {output}")
    for name, expected in (
            ("hello-result.txt", "Hello, world!\n"),
            ("relative-result.txt", "Hello, world!\n"),
            ("class-result.txt", "Hello, world!\n"),
            ("runtime-result.txt", "OPENJDK RUNTIME PASS\n"),
            ("nio-result.txt", "OPENJDK NIO PASS\n")):
        result = subprocess.check_output(["mtype", "-i", str(disk), f"::/java/{name}"], text=True)
        (output / name).write_text(result)
        if result != expected:
            raise RuntimeError(f"OpenJDK {name} failed: {result.strip()}; see {output}")
    print(f"OPENJDK SOURCE/CLASS, RUNTIME AND NIO PASS: {output}")
    timings = subprocess.check_output(
        ["mtype", "-i", str(disk), "::/java/startup-times.txt"], text=True)
    (output / "startup-times.txt").write_text(timings)
    seconds = [float(value) for value in timings.splitlines()]
    if len(seconds) != 3 or any(value <= 0 for value in seconds):
        raise RuntimeError(f"Invalid startup timings: {timings!r}")
    print(f"Java startup seconds (cold, warm, warm): {seconds}")
    if args.jit:
        source_log = subprocess.check_output(
            ["mtype", "-i", str(disk), "::/java/source-jit.xml"], text=True)
        (output / "source-jit.xml").write_text(source_log)
        source_compilation = ET.fromstring(source_log)
        entries = {method.get("compile_id") for method in source_compilation.iter("nmethod")
                   if method.get("compile_kind") != "osr"}
        if not any(event.get("compile_id") in entries and event.get("zombie") != "1"
                   for event in source_compilation.iter("make_not_entrant")):
            raise RuntimeError(f"Source launch did not exercise compiled entry invalidation; see {output}")
        print(f"OPENJDK ENTRY PATCH PASS: {output}")
        workload = subprocess.check_output(
            ["mtype", "-i", str(disk), "::/java/jit-result.txt"], text=True)
        compilation = subprocess.check_output(
            ["mtype", "-i", str(disk), "::/java/jit.xml"], text=True)
        (output / "jit-result.txt").write_text(workload)
        (output / "jit.xml").write_text(compilation)
        methods = ET.fromstring(compilation).iter("nmethod")
        if (workload != "OPENJDK JIT WORKLOAD PASS\n" or
                "Jit C:/java/jit-result.txt status=0" not in text or
                not any(method.get("compiler") == args.jit and
                        method.get("method") == "Jit kernel (I)J" for method in methods)):
            raise RuntimeError(f"{args.jit} did not compile and execute the JIT workload; see {output}")
        print(f"OPENJDK {args.jit.upper()} JIT PASS: {output}")


if __name__ == "__main__":
    main()
