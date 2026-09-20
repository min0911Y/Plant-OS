#!/usr/bin/env python3
"""Boot and exercise an unmodified Minecraft 1.20.1 server on Plant OS."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib
from io import BytesIO
import json
from pathlib import Path
import re
import socket
import struct
import subprocess
import time
import zipfile


ROOT = Path(__file__).resolve().parents[1]
APPS = ROOT / "apps"
EXPECTED_SERVER_SHA256 = "3af73a9dc5a102e38147946360dd27d4d70bae7055bf91cf2151cd5d121b79e0"
EXPECTED_INNER_SHA256 = "80db52b203ac5de6e5fc1c5082259df440fb2b5390b4c61d474e8fbc63cc41f5"
INNER_SERVER = "META-INF/versions/1.20.1/server-1.20.1.jar"
RCON_COMMAND_TIMEOUT = 180
JVM_MODE_OPTIONS = {
    "default": (),
    "xint": ("-Xint",),
    "c1": ("-XX:+TieredCompilation", "-XX:TieredStopAtLevel=1"),
    "c2": ("-XX:-TieredCompilation",),
    "default-sse": ("-XX:UseAVX=0",),
    "c2-sse": ("-XX:-TieredCompilation", "-XX:UseAVX=0"),
}


def server_command(jvm_mode, xms="256m", xmx="1536m"):
    options = " ".join(JVM_MODE_OPTIONS[jvm_mode])
    if options:
        options += " "
    return (f"C:/java/bin/java {options}-Xms{xms} -Xmx{xmx} "
            "-jar C:/java/mc/server.jar nogui")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_server_jar(path):
    actual = sha256(path)
    if actual != EXPECTED_SERVER_SHA256:
        raise RuntimeError(f"unexpected server JAR SHA-256: {actual}")
    with zipfile.ZipFile(path) as outer:
        inner = outer.read(INNER_SERVER)
    if hashlib.sha256(inner).hexdigest() != EXPECTED_INNER_SHA256:
        raise RuntimeError("inner Minecraft server JAR SHA-256 does not match")
    with zipfile.ZipFile(BytesIO(inner)) as archive:
        names = set(archive.namelist())
    required = {"META-INF/MOJANGCS.SF", "META-INF/MOJANGCS.RSA"}
    if not required <= names:
        raise RuntimeError(f"inner server JAR is missing signatures: {sorted(required - names)}")


def reserve_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def encode_varint(value):
    encoded = bytearray()
    value &= 0xffffffff
    while True:
        byte = value & 0x7f
        value >>= 7
        encoded.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(encoded)


def read_exact(connection, length):
    result = bytearray()
    while len(result) < length:
        block = connection.recv(length - len(result))
        if not block:
            raise ConnectionError("connection closed before a complete packet")
        result.extend(block)
    return bytes(result)


def read_varint(connection):
    value = 0
    for index in range(5):
        byte = read_exact(connection, 1)[0]
        value |= (byte & 0x7f) << (index * 7)
        if not byte & 0x80:
            return value
    raise ValueError("VarInt is too long")


def minecraft_status(port):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
        connection.settimeout(5)
        address = b"127.0.0.1"
        handshake = (encode_varint(0) + encode_varint(763) +
                     encode_varint(len(address)) + address +
                     struct.pack(">H", 25565) + encode_varint(1))
        connection.sendall(encode_varint(len(handshake)) + handshake + b"\x01\x00")
        length = read_varint(connection)
        payload = memoryview(read_exact(connection, length))
        if not payload or payload[0] != 0:
            raise RuntimeError("invalid Minecraft status response packet")
        cursor = 1
        json_length = 0
        shift = 0
        while True:
            byte = payload[cursor]
            cursor += 1
            json_length |= (byte & 0x7f) << shift
            if not byte & 0x80:
                break
            shift += 7
        result = json.loads(bytes(payload[cursor:cursor + json_length]))
        if result.get("version", {}).get("protocol") != 763:
            raise RuntimeError(f"unexpected server protocol: {result.get('version')}")
        nonce = time.monotonic_ns() & ((1 << 63) - 1)
        ping = b"\x01" + struct.pack(">q", nonce)
        connection.sendall(encode_varint(len(ping)) + ping)
        pong_length = read_varint(connection)
        pong = read_exact(connection, pong_length)
        if pong != ping:
            raise RuntimeError("Minecraft status pong did not match ping")
        return result


class Rcon:
    def __init__(self, port, password):
        self.connection = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.connection.settimeout(RCON_COMMAND_TIMEOUT)
        self.request = 1
        response_id, _, _ = self._exchange(3, password)
        if response_id == -1:
            raise RuntimeError("RCON authentication failed")

    def close(self):
        self.connection.close()

    def _exchange(self, packet_type, value):
        request_id = self.request
        self.request += 1
        body = struct.pack("<ii", request_id, packet_type) + value.encode() + b"\0\0"
        self.connection.sendall(struct.pack("<i", len(body)) + body)
        size = struct.unpack("<i", read_exact(self.connection, 4))[0]
        if size < 10 or size > 4 * 1024 * 1024:
            raise RuntimeError(f"invalid RCON response size: {size}")
        response = read_exact(self.connection, size)
        response_id, response_type = struct.unpack_from("<ii", response)
        return response_id, response_type, response[8:-2].decode(errors="replace")

    def command(self, value):
        response_id, _, response = self._exchange(2, value)
        if response_id < 0:
            raise RuntimeError(f"RCON command failed: {value}")
        return response


def write_guest_inputs(directory, level_type, command):
    directory.mkdir(parents=True)
    (directory / "eula.txt").write_text("eula=true\n")
    properties = {
        "allow-flight": "true",
        "difficulty": "peaceful",
        "enable-command-block": "false",
        "enable-jmx-monitoring": "false",
        "enable-query": "false",
        "enable-rcon": "true",
        "enforce-secure-profile": "false",
        "force-gamemode": "false",
        "gamemode": "creative",
        "generate-structures": "true",
        "level-name": "world",
        "level-seed": "8675309",
        "level-type": level_type,
        "max-players": "8",
        "motd": "Plant OS Minecraft acceptance",
        "online-mode": "false",
        "rcon.password": "plant-minecraft-acceptance",
        "rcon.port": "25575",
        "server-ip": "",
        "server-port": "25565",
        "simulation-distance": "3",
        "spawn-animals": "false",
        "spawn-monsters": "false",
        "spawn-npcs": "false",
        "spawn-protection": "0",
        "sync-chunk-writes": "true",
        "view-distance": "3",
    }
    (directory / "server.properties").write_text(
        "".join(f"{key}={value}\n" for key, value in sorted(properties.items())))
    (directory / "run-minecraft.lua").write_text(
        'assert(os.execute("cd C:/java/mc"))\n'
        f'assert(os.execute("{command}"))\n'
    )


def build_base(args, output, iso, disk):
    guest = output / "guest-inputs"
    write_guest_inputs(guest, args.level_type, args.server_command)
    with (output / "build.log").open("w") as log:
        for directory in (APPS, ROOT / "kernel"):
            subprocess.run(["make", "-C", str(directory), "ARCH=x86_64", "-j8"],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["python3", str(ROOT / "scripts/build-minecraft.py"),
                        "--jdk", str(args.jdk.resolve()), "--server", str(args.server.resolve()),
                        "--iso", str(iso), "--disk", str(disk), "--disk-mib", str(args.disk_mib),
                        "--config", str(guest)], stdout=log, stderr=subprocess.STDOUT, check=True)


def connect_rcon(port, running, deadline, serial):
    last_error = None
    while time.monotonic() < deadline and running():
        text = serial.read_text(errors="replace") if serial.exists() else ""
        if "A fatal error has been detected" in text or "PANIC" in text:
            raise RuntimeError("guest failed before Minecraft became ready")
        try:
            return Rcon(port, "plant-minecraft-acceptance")
        except (ConnectionError, OSError, RuntimeError) as error:
            last_error = error
            time.sleep(1)
    raise RuntimeError(f"Minecraft RCON did not become ready: {last_error}")


def exercise_server(args, output, server_port, rcon_port, running):
    transcript = {}
    rcon = None
    try:
        rcon = connect_rcon(rcon_port, running,
                            time.monotonic() + args.timeout,
                            output / "serial.log")
        with ThreadPoolExecutor(max_workers=6) as pool:
            statuses = list(pool.map(lambda _: minecraft_status(server_port), range(12)))
        transcript["status"] = statuses[0]
        transcript["status_connection_count"] = len(statuses)
        transcript["list"] = rcon.command("list")
        transcript["save_all"] = rcon.command("save-all flush")
        transcript["jfr_start"] = rcon.command("jfr start")
        time.sleep(args.jfr_seconds)
        transcript["jfr_stop"] = rcon.command("jfr stop")
        transcript["stop"] = rcon.command("stop")
    finally:
        (output / "rcon.json").write_text(json.dumps(transcript, indent=2) + "\n")
        if rcon is not None:
            rcon.close()


def wait_for_shutdown(qmp, serial, deadline):
    while time.monotonic() < deadline:
        text = serial.read_text(errors="replace") if serial.exists() else ""
        if ("acpi: entering S5" in text and
                qmp.execute("query-status")["status"] == "shutdown"):
            qmp.execute("quit")
            return
        time.sleep(0.2)
    raise RuntimeError("Minecraft guest did not shut down cleanly")


def run_guest(args, output, iso, disk):
    server_port = reserve_port()
    rcon_port = reserve_port()
    while rcon_port == server_port:
        rcon_port = reserve_port()
    serial = output / "serial.log"
    qmp_path = output / "qmp.sock"
    qmp_path.unlink(missing_ok=True)
    accelerator = "tcg,thread=multi" if args.accel == "tcg" else "kvm"
    cpu = args.cpu or ("host" if args.accel == "kvm" else "max,-xgetbv1")
    command = [
        "qemu-system-x86_64", "-accel", accelerator,
        "-cpu", cpu,
        "-smp", "4", "-m", str(args.memory), "-display", "gtk",
        "-monitor", "none", "-no-reboot", "-no-shutdown",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
        "-serial", f"file:{serial}", "-cdrom", str(iso), "-boot", "d",
        "-drive", f"file={disk},format=raw,if=ide,index=0",
        "-netdev", (f"user,id=minecraft,hostfwd=tcp:127.0.0.1:{server_port}-:25565,"
                    f"hostfwd=tcp:127.0.0.1:{rcon_port}-:25575"),
        "-device", "pcnet,netdev=minecraft",
    ]
    (output / "qemu-command.json").write_text(json.dumps(command, indent=2) + "\n")
    with (output / "qemu.log").open("w") as qemu_log:
        guest = subprocess.Popen(command, stdout=qemu_log, stderr=subprocess.STDOUT)
        qmp = None
        deadline = time.monotonic() + args.timeout
        try:
            qmp_class = importlib.import_module("test-x86_64").QMP
            while qmp is None and time.monotonic() < deadline:
                try:
                    qmp = qmp_class(qmp_path)
                except (FileNotFoundError, ConnectionRefusedError, TimeoutError):
                    time.sleep(0.1)
            if qmp is None:
                raise RuntimeError("QMP did not become ready")
            exercise_server(args, output, server_port, rcon_port,
                            lambda: guest.poll() is None)
            wait_for_shutdown(qmp, serial, deadline)
            guest.wait(timeout=10)
            if guest.returncode != 0:
                raise RuntimeError(f"QEMU exited with status {guest.returncode}")
        finally:
            if qmp is not None:
                qmp.close()
            if guest.poll() is None:
                guest.kill()
            guest.wait()


def resume_guest(args, output):
    command = json.loads((output / "qemu-command.json").read_text())
    network = command[command.index("-netdev") + 1]
    forwards = {int(guest): int(host) for host, guest in re.findall(
        r"hostfwd=tcp:127\.0\.0\.1:(\d+)-:(\d+)", network)}
    if 25565 not in forwards or 25575 not in forwards:
        raise RuntimeError("saved QEMU command has no Minecraft/RCON forwarding")
    qmp = importlib.import_module("test-x86_64").QMP(output / "qmp.sock")
    try:
        running = lambda: qmp.execute("query-status")["status"] == "running"
        exercise_server(args, output, forwards[25565], forwards[25575], running)
        wait_for_shutdown(qmp, output / "serial.log",
                          time.monotonic() + args.timeout)
    finally:
        qmp.close()


def copy_guest_file(disk, guest_path, destination):
    result = subprocess.run(["mcopy", "-o", "-i", str(disk), guest_path,
                             str(destination)], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    return result.returncode == 0


def collect_and_validate(args, output, iso, disk):
    serial = (output / "serial.log").read_text(errors="replace")
    copy_guest_file(disk, "::/java/mc/logs/latest.log", output / "latest.log")
    copy_guest_file(disk, "::/java/mc/hs_err.log", output / "hs_err.log")
    listing = subprocess.check_output(
        ["mdir", "-/", "-b", "-i", str(disk), "::/java/mc"],
        text=True, stderr=subprocess.STDOUT)
    (output / "guest-files.txt").write_text(listing)
    latest = (output / "latest.log").read_text(errors="replace")
    failures = ("A fatal error has been detected", "Unsupported Management version",
                "Flight Recorder is not supported", "Accept failed",
                "Failed to close socket", "ArrayIndexOutOfBoundsException",
                "IllegalArgumentException", "Error executing task on Chunk source",
                "OutOfMemoryError", "attempting force stop")
    if (f"init: command lua.bin C:/java/mc/run-minecraft.lua status=0" not in serial or
            "acpi: entering S5" not in serial or "Done (" not in latest or
            "All dimensions are saved" not in latest or
            any(failure in serial or failure in latest for failure in failures) or
            (output / "hs_err.log").exists()):
        raise RuntimeError(f"Minecraft acceptance checks failed; see {output}")
    if ".jfr" not in listing.lower():
        raise RuntimeError("Minecraft did not create a JFR recording")
    summaries = {
        "server.jar": sha256(args.server),
        "java": sha256(args.jdk / "bin/java"),
        "libjvm.so": sha256(args.jdk / "lib/server/libjvm.so"),
        "kernel.bin": sha256(ROOT / "kernel/obj/x86_64/kernel.bin"),
        "test.iso": sha256(iso),
        "test-jdk.img": sha256(disk),
        "latest.log": sha256(output / "latest.log"),
    }
    (output / "sha256.json").write_text(json.dumps(summaries, indent=2) + "\n")
    print(f"MINECRAFT 1.20.1 NEW WORLD, NETWORK, SAVE, JFR AND STOP PASS: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jdk", type=Path, default=ROOT / "apps/out/x86_64/openjdk/images/jdk")
    parser.add_argument("--server", type=Path, default=ROOT / "apps/out/sources/minecraft-server-1.20.1.jar")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--accel", choices=("tcg", "kvm"), default="tcg")
    parser.add_argument("--cpu", choices=("host", "max,-xgetbv1"),
                        help="QEMU CPU model (default follows --accel)")
    parser.add_argument("--memory", type=int, default=4096, help="guest RAM in MiB")
    parser.add_argument("--disk-mib", type=int, default=1536)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--jfr-seconds", type=int, default=5)
    parser.add_argument("--level-type", default="minecraft:normal")
    parser.add_argument("--xms", default="256m", help="initial Java heap, for example 256m")
    parser.add_argument("--xmx", default="1536m", help="maximum Java heap, for example 1536m")
    parser.add_argument("--jvm-mode", choices=tuple(JVM_MODE_OPTIONS), default="default",
                        help="server compiler mode (default, xint, c1 or c2)")
    parser.add_argument("--resume", action="store_true",
                        help="reconnect to the QEMU process saved in --out")
    args = parser.parse_args()
    args.jdk = args.jdk.resolve()
    args.server = args.server.resolve()
    args.server_command = server_command(args.jvm_mode, args.xms, args.xmx)
    output = args.out.resolve()
    if not args.resume and output.exists() and any(output.iterdir()):
        parser.error(f"--out must be empty so evidence is not overwritten: {output}")
    if not (args.jdk / "bin/java").is_file():
        parser.error(f"invalid Plant JDK: {args.jdk}")
    if args.memory < 1536 or args.disk_mib < 768 or args.timeout < 60:
        parser.error("Minecraft needs at least 1536 MiB RAM, 768 MiB disk and 60 seconds")
    validate_server_jar(args.server)
    iso = output / "test.iso"
    disk = output / "test-jdk.img"
    if args.resume:
        if not iso.is_file() or not disk.is_file():
            parser.error(f"--resume output is incomplete: {output}")
        resume_guest(args, output)
        collect_and_validate(args, output, iso, disk)
        return
    output.mkdir(parents=True, exist_ok=True)
    (output / "configuration.json").write_text(json.dumps({
        "accel": args.accel,
        "cpu": args.cpu or ("host" if args.accel == "kvm" else "max,-xgetbv1"),
        "memory_mib": args.memory,
        "disk_mib": args.disk_mib,
        "level_type": args.level_type,
        "server_command": args.server_command,
        "server_sha256": EXPECTED_SERVER_SHA256,
    }, indent=2) + "\n")
    build_base(args, output, iso, disk)
    run_guest(args, output, iso, disk)
    collect_and_validate(args, output, iso, disk)


if __name__ == "__main__":
    main()
