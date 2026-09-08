#!/usr/bin/env python3
"""Build native graphics dependencies with host generators kept separate."""
import argparse
import fcntl
import hashlib
import json
import os
import re
import shlex
import struct
import sys
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
APPS = ROOT / "apps"
PORT = APPS / "mesa"
SOURCES = APPS / "out/sources"


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def publish(path, text):
    if path.exists() and path.read_text() == text:
        return
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text)
    temporary.replace(path)


def source(name):
    spec = json.loads((PORT / "sources.json").read_text())[name]
    patches = sorted((PORT / "patches" / name).glob("*.patch"))
    patch_hash = hashlib.sha256(b"".join(patch.read_bytes() for patch in patches)).hexdigest()
    changed_files = set()
    for patch in patches:
        for line in patch.read_text().splitlines():
            if line.startswith(("--- a/", "+++ b/")):
                relative = line[6:].split("\t", 1)[0]
                if Path(relative).is_absolute() or ".." in Path(relative).parts:
                    raise RuntimeError(f"invalid patch path: {relative}")
                changed_files.add(relative)
    state = {"archive": spec["sha256"], "patch": patch_hash, "files": sorted(changed_files)}
    SOURCES.mkdir(parents=True, exist_ok=True)
    with (SOURCES / ".lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        archive = SOURCES / spec["url"].rsplit("/", 1)[1]
        directory = SOURCES / spec["directory"]
        marker = directory / ".plant-source-sha256"
        previous = None
        if marker.exists() and marker.read_text().lstrip().startswith("{"):
            previous = json.loads(marker.read_text())
        if previous == state:
            return directory
        if not archive.exists():
            temporary = archive.with_name(archive.name + ".part")
            print(f"Downloading {name} {spec['version']}", flush=True)
            try:
                with urllib.request.urlopen(spec["url"], timeout=60) as response, temporary.open("wb") as output:
                    shutil.copyfileobj(response, output)
                if digest(temporary) != spec["sha256"]:
                    raise RuntimeError(f"checksum mismatch: {archive.name}")
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
        if digest(archive) != spec["sha256"]:
            raise RuntimeError(f"checksum mismatch: {archive}")
        if not directory.exists():
            with tempfile.TemporaryDirectory(prefix="extract-", dir=SOURCES) as temporary:
                with tarfile.open(archive) as package:
                    package.extractall(temporary, filter="data")
                (Path(temporary) / spec["directory"]).replace(directory)
        if changed_files or (previous and previous["files"]):
            files = changed_files | set(previous["files"] if previous else [])
            with tempfile.TemporaryDirectory(prefix="patch-", dir=SOURCES) as temporary:
                staging = Path(temporary)
                for relative in files:
                    if Path(relative).is_absolute() or ".." in Path(relative).parts:
                        raise RuntimeError(f"invalid cached patch path: {relative}")
                prefix = spec["directory"] + "/"
                # Stream once: random extraction repeatedly decompresses a large
                # LLVM tar.xz when patches touch files in different directories.
                with tarfile.open(archive, "r|*") as package:
                    for member in package:
                        relative = member.name.removeprefix(prefix)
                        if not member.name.startswith(prefix) or relative not in files:
                            continue
                        if not member.isfile():
                            raise RuntimeError(f"patch target is not a regular file: {relative}")
                        destination = staging / relative
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        destination.write_bytes(package.extractfile(member).read())
                        destination.chmod(member.mode & 0o777)
                for patch in patches:
                    subprocess.run(["patch", "--batch", "-p1", "-i", str(patch)], cwd=staging, check=True)
                for relative in files:
                    staged, destination = staging / relative, directory / relative
                    if not staged.exists():
                        destination.unlink(missing_ok=True)
                    elif not destination.exists() or destination.read_bytes() != staged.read_bytes():
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        staged.replace(destination)
        publish(marker, json.dumps(state, sort_keys=True) + "\n")
        return directory


def host_tool(name):
    candidates = [shutil.which(name), APPS / f"out/host/venv/bin/{name}",
                  APPS / f"out/host/debian/root/usr/bin/{name}"]
    if name == "cmake":
        candidates.append(APPS / "out/host/python/cmake/data/bin/cmake")
    else:
        candidates.append(APPS / f"out/host/python/bin/{name}")
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise RuntimeError(f"missing host tool {name}; install the graphics build dependencies described in doc/lavapipe.md")


def run(arguments):
    subprocess.run([str(argument) for argument in arguments], check=True)


def build_libcxx(arch, output, jobs):
    llvm = source("llvm")
    build = output / "mesa/libcxx"
    build.mkdir(parents=True, exist_ok=True)
    cmake = host_tool("cmake")
    ninja = host_tool("ninja")
    clang = host_tool("clang")
    options = [
        f"-DCMAKE_TOOLCHAIN_FILE={PORT / 'toolchain.cmake'}",
        f"-DPLOS_ARCH={arch}", f"-DCMAKE_MAKE_PROGRAM={ninja}",
        "-DCMAKE_BUILD_TYPE=Release", "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi",
        "-DLIBCXX_ENABLE_SHARED=OFF", "-DLIBCXXABI_ENABLE_SHARED=OFF",
        "-DLIBCXXABI_USE_LLVM_UNWINDER=OFF", "-DLIBCXX_ENABLE_EXCEPTIONS=OFF",
        "-DLIBCXX_ENABLE_RTTI=OFF", "-DLIBCXXABI_ENABLE_EXCEPTIONS=OFF",
        "-DLIBCXXABI_ENABLE_RTTI=OFF",
        "-DLIBCXX_INCLUDE_TESTS=OFF", "-DLIBCXXABI_INCLUDE_TESTS=OFF",
        "-DLIBCXX_ENABLE_FILESYSTEM=OFF", "-DLIBCXX_ENABLE_RANDOM_DEVICE=ON",
        "-DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF", "-DLIBCXX_HAS_TERMINAL_AVAILABLE=OFF",
        "-DLIBCXX_HAS_PTHREAD_API=ON", "-DLIBCXXABI_HAS_PTHREAD_API=ON",
        "-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON", "-DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF",
        "-DLIBCXX_EXTRA_SITE_DEFINES=_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE;_LIBCPP_HAS_CLOCK_GETTIME",
    ]
    fingerprint = json.dumps({
        "options": options,
        "sources": digest(PORT / "sources.json"),
        "patches": {path.name: digest(path) for path in sorted((PORT / "patches/llvm").glob("*.patch"))
                    if "--- a/libcxx" in path.read_text()},
        "toolchain": {str(path.relative_to(ROOT)): digest(path) for path in
                      [PORT / "toolchain.cmake", PORT / "cmake/Platform/PlantOS.cmake", APPS / "dynamic.mk"]},
        "compiler": subprocess.check_output([clang, "--version"], text=True),
        "cmake": subprocess.check_output([cmake, "--version"], text=True),
    }, sort_keys=True)
    configured = build / ".plant-config"
    if not configured.exists() or configured.read_text() != fingerprint or not (build / "build.ninja").exists():
        run([cmake, "--fresh", "-G", "Ninja", "-S", llvm / "runtimes", "-B", build, *options])
        publish(configured, fingerprint)
    run([ninja, "-C", build, "cxx_static", "-j", jobs])
    # cxx_static already contains libc++abi when STATIC_ABI_LIBRARY is enabled.
    publish(output / "mesa/libcxx.stamp", digest(build / "lib/libc++.a") + "\n")


def build_llvm(arch, output, jobs):
    if arch != "x86_64":
        raise RuntimeError("the native LLVM/Mesa backend currently targets x86_64")
    for name in ("libp.so", "libcpp.so"):
        if not (output / "lib" / name).exists():
            raise RuntimeError(f"build the native runtime before LLVM: missing {name}")
    llvm = source("llvm")
    build = output / "mesa/llvm"
    build.mkdir(parents=True, exist_ok=True)
    cmake, ninja = host_tool("cmake"), host_tool("ninja")
    clang = host_tool("clang")
    tablegen = shutil.which("llvm-tblgen") or str(Path(clang).resolve().parent / "llvm-tblgen")
    if not Path(tablegen).is_file():
        raise RuntimeError("a host llvm-tblgen matching the pinned LLVM version is required")
    version = json.loads((PORT / "sources.json").read_text())["llvm"]["version"]
    tablegen_version = subprocess.check_output([tablegen, "--version"], text=True)
    if not re.search(r"\bversion\s+" + re.escape(version) + r"\b", tablegen_version):
        raise RuntimeError(f"host llvm-tblgen must match LLVM {version}: {tablegen}")
    options = [
        f"-DCMAKE_TOOLCHAIN_FILE={PORT / 'toolchain.cmake'}", f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DPLOS_ARCH={arch}", "-DPLOS_USE_LIBCXX=ON", "-DCMAKE_BUILD_TYPE=Release",
        "-DLLVM_TARGETS_TO_BUILD=X86", f"-DLLVM_DEFAULT_TARGET_TRIPLE={arch}-unknown-none-elf",
        f"-DLLVM_HOST_TRIPLE={arch}-unknown-none-elf", f"-DLLVM_TABLEGEN={tablegen}",
        "-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_EXAMPLES=OFF",
        "-DLLVM_BUILD_TOOLS=OFF", "-DLLVM_BUILD_UTILS=OFF", "-DLLVM_ENABLE_EH=OFF",
        "-DLLVM_ENABLE_RTTI=OFF", "-DLLVM_ENABLE_PIC=ON", "-DLLVM_ENABLE_THREADS=ON",
        "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF", "-DLLVM_ENABLE_LIBXML2=OFF",
        "-DLLVM_ENABLE_LIBEDIT=OFF", "-DLLVM_ENABLE_LIBCXX=OFF", "-DLLVM_ENABLE_LIBPFM=OFF",
        "-DLLVM_ENABLE_FFI=OFF", "-DLLVM_ENABLE_BACKTRACES=OFF", "-DLLVM_ENABLE_CRASH_OVERRIDES=OFF",
        "-DLLVM_ENABLE_ASSERTIONS=OFF",
    ]
    symbols = subprocess.check_output(["nm", "-D", "--defined-only", "--format=posix",
                                       output / "lib/libp.so"], text=True)
    fingerprint = json.dumps({
        "options": options, "patches": digest(llvm / ".plant-source-sha256"),
        "toolchain": {str(path.relative_to(ROOT)): digest(path) for path in
                      [PORT / "toolchain.cmake", PORT / "cmake/Platform/PlantOS.cmake", APPS / "dynamic.mk"]},
        "headers": {str(path.relative_to(APPS / "include")): digest(path)
                    for path in sorted((APPS / "include").rglob("*.h"))},
        "runtime_symbols": sorted(" ".join(line.split()[:2]) for line in symbols.splitlines()),
        "compiler": subprocess.check_output([clang, "--version"], text=True),
        "tablegen": tablegen_version,
    }, sort_keys=True)
    configured = build / ".plant-config"
    if not configured.exists() or configured.read_text() != fingerprint:
        run([cmake, "--fresh", "-G", "Ninja", "-S", llvm / "llvm", "-B", build, *options])
        publish(configured, fingerprint)
    targets = ["LLVMCore", "LLVMExecutionEngine", "LLVMMCJIT", "LLVMMCDisassembler",
               "LLVMX86CodeGen", "LLVMX86AsmParser", "LLVMX86Disassembler", "LLVMScalarOpts",
               "LLVMTransformUtils", "LLVMInstCombine", "LLVMBitWriter", "LLVMCoroutines", "LLVMPasses"]
    graph = subprocess.check_output([ninja, "-C", str(build), "-t", "graph", *targets], text=True)
    archives = sorted(set(re.findall(r'label="(lib/libLLVM[^"/]+\.a)"', graph)))
    if not archives:
        raise RuntimeError("LLVM dependency graph did not identify any component archives")
    publish(build / "components.json", json.dumps(archives, indent=2) + "\n")
    run([ninja, "-C", build, *targets, "-j", jobs])
    archive = build / "lib/libLLVMPlant.a"
    archive_state = json.dumps({name: [(build / name).stat().st_size, (build / name).stat().st_mtime_ns]
                               for name in archives}, sort_keys=True)
    state_path = build / ".plant-archive"
    if archive.exists() and state_path.exists() and state_path.read_text() == archive_state:
        if not (output / "mesa/llvm.stamp").exists():
            publish(output / "mesa/llvm.stamp", digest(archive) + "\n")
        return
    temporary = archive.with_suffix(".tmp.a")
    archiver = str(Path(clang).resolve().parent / "llvm-ar")
    script = f"create {temporary}\n" + "".join(f"addlib {build / name}\n" for name in archives) + "save\nend\n"
    subprocess.run([archiver, "-M"], input=script, text=True, check=True)
    temporary.replace(archive)
    publish(state_path, archive_state)
    publish(output / "mesa/llvm.stamp", digest(archive) + "\n")


def configure_mesa(arch, output):
    if arch != "x86_64":
        raise RuntimeError("native Mesa currently requires x86_64")
    mesa = source("mesa")
    build = output / "mesa/driver"
    sysroot = output / "mesa/sysroot"
    (sysroot / "lib/pkgconfig").mkdir(parents=True, exist_ok=True)
    flags = shlex.split(subprocess.check_output(
        ["make", "--no-print-directory", "-s", "-C", APPS, "-f", "dynamic.mk",
         f"ARCH={arch}", "print-runtime-flags"], text=True))
    linker_flags = shlex.split(subprocess.check_output(
        ["make", "--no-print-directory", "-s", "-C", APPS, "-f", "dynamic.mk",
         f"ARCH={arch}", "print-link-flags"], text=True))
    resource = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    flags += ["-nostdlibinc", "-resource-dir=" + resource, "-I" + str(APPS / "include"),
              "-I" + str(PORT / "include")]
    link = ["-nostdlib", "-Wl," + ",".join(linker_flags),
            "-Wl,--exclude-libs,libLLVMPlant.a", "-L" + str(sysroot / "lib"),
            str(output / "dynamic/libp/dso.o"), str(output / "lib/libp.so")]
    clang = host_tool("clang")
    clangxx = host_tool("clang++")
    cross = {
        "binaries": {
            "c": [clang, f"--target={arch}-unknown-none-elf", "--sysroot=" + str(sysroot)],
            "cpp": [clangxx, f"--target={arch}-unknown-none-elf", "--sysroot=" + str(sysroot)],
            "ar": str(Path(clang).resolve().parent / "llvm-ar"),
            "strip": "strip", "pkg-config": "pkg-config",
            "llvm-config": [sys.executable, str(PORT / "llvm-config.py"), str(output)],
        },
        "host_machine": {"system": "plantos", "cpu_family": arch, "cpu": arch, "endian": "little"},
        "properties": {"needs_exe_wrapper": True, "pkg_config_libdir": str(sysroot / "lib/pkgconfig")},
        "built-in options": {
            "c_args": flags,
            "cpp_args": ["-I" + str(output / "mesa/libcxx/include/c++/v1"), *flags,
                         "-nostdinc++", "-fno-exceptions", "-fno-rtti"],
            "c_link_args": link,
            "cpp_link_args": [*link, str(output / "lib/libcpp.so")],
            "b_asneeded": False,
        },
    }
    cross_path = output / "mesa/cross.ini"
    def meson_value(value):
        if isinstance(value, bool):
            return str(value).lower()
        return repr(value)
    publish(cross_path, "\n".join(
        f"[{section}]\n" + "\n".join(f"{key} = {meson_value(value)}" for key, value in values.items())
        for section, values in cross.items()) + "\n")
    options = [
        "--buildtype=release", "--wrap-mode=nofallback", "--auto-features=disabled",
        "-Dplatforms=[]", "-Dgallium-drivers=llvmpipe", "-Dvulkan-drivers=swrast",
        "-Dllvm=enabled", "-Dshared-llvm=disabled", "-Ddraw-use-llvm=true", "-Dllvm-orcjit=false",
        "-Dcpp_rtti=false", "-Dopengl=true", "-Dgles1=disabled", "-Dgles2=disabled",
        "-Dglx=disabled", "-Degl=enabled", "-Dgbm=disabled", "-Dshader-cache=disabled",
        "-Dxmlconfig=disabled", "-Dzlib=disabled", "-Dvideo-codecs=[]", "-Dbuild-tests=false",
        "-Dtools=[]", "-Dplantos-port=" + str(PORT / "mesa"),
    ]
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(APPS / "out/host/python")
    environment["PATH"] = str(Path(host_tool("ninja")).parent) + os.pathsep + environment["PATH"]
    environment["PATH"] = str(Path(host_tool("glslangValidator")).parent) + os.pathsep + environment["PATH"]
    for generator in ("bison", "flex", "m4"):
        environment["PATH"] = str(Path(host_tool(generator)).parent) + os.pathsep + environment["PATH"]
    environment["M4"] = host_tool("m4")
    bison_data = Path(host_tool("bison")).parent.parent / "share/bison"
    if bison_data.is_dir():
        environment["BISON_PKGDATADIR"] = str(bison_data)
    symbols = subprocess.check_output(["nm", "-D", "--defined-only", "--format=posix",
                                       output / "lib/libp.so"], text=True)
    fingerprint = json.dumps({"cross": cross, "options": options,
        "compiler": subprocess.check_output([clang, "--version"], text=True),
        "runtime_symbols": sorted(" ".join(line.split()[:2]) for line in symbols.splitlines()),
        "source": digest(mesa / ".plant-source-sha256"),
        "headers": {str(path.relative_to(APPS / "include")): digest(path)
                    for path in sorted((APPS / "include").rglob("*.h"))}}, sort_keys=True)
    configured = build / ".plant-config"
    if not configured.exists() or configured.read_text() != fingerprint:
        command = [sys.executable, "-m", "mesonbuild.mesonmain", "setup"]
        if (build / "meson-private/coredata.dat").exists():
            previous = json.loads(configured.read_text()) if configured.exists() else {}
            if previous.get("cross") != cross:
                command.append("--wipe")
            else:
                command += ["--clearcache", "--reconfigure"]
        subprocess.run([*command, str(build), str(mesa), "--cross-file", str(cross_path), *options],
                       env=environment, check=True)
        publish(configured, fingerprint)
    return build, environment


def build_mesa(arch, output, jobs):
    if not (output / "mesa/llvm/lib/libLLVMPlant.a").exists():
        raise RuntimeError("build the native LLVM component before Mesa")
    build, environment = configure_mesa(arch, output)
    libraries = {
        "src/gallium/targets/lavapipe/liblvp.so": ("vk_icdGetInstanceProcAddr", "plant_vulkan_create_surface"),
        "src/egl/libEGL.so": ("eglCreateContext", "eglGetProcAddress"),
        "src/egl/libGL.so": ("glBegin", "glCreateShader"),
    }
    subprocess.run([host_tool("ninja"), "-C", str(build), *libraries, "-j", str(jobs)],
                   env=environment, check=True)
    for target, required in libraries.items():
        library = build / target
        symbols = subprocess.check_output(["nm", "-D", "--defined-only", library], text=True)
        for symbol in required:
            if not re.search(r"\b" + symbol + r"$", symbols, re.MULTILINE):
                raise RuntimeError(f"{library.name} does not export {symbol}")
    for target in libraries:
        destination = output / "lib" / Path(target).name
        if destination.exists() and digest(destination) == digest(build / target):
            continue
        temporary = destination.with_suffix(".tmp")
        shutil.copyfile(build / target, temporary)
        temporary.replace(destination)
    mesa = source("mesa")
    header_state = {}
    for headers in ("EGL", "GL", "KHR"):
        for header in sorted((mesa / "include" / headers).glob("*.h")):
            destination = output / "mesa/include" / headers / header.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            publish(destination, header.read_text())
            header_state[f"{headers}/{header.name}"] = digest(header)
    publish(output / "mesa/headers.stamp", json.dumps(header_state, sort_keys=True) + "\n")


def build_shaders(arch, output, source):
    if arch != "x86_64":
        raise RuntimeError("the Vulkan regression requires x86_64")
    directory = output / source.parent.name
    directory.mkdir(parents=True, exist_ok=True)
    name = source.name.replace(".", "_")
    binary = directory / (name + ".spv")
    run([host_tool("glslangValidator"), "-V", source, "-o", binary])
    data = binary.read_bytes()
    if len(data) % 4 or data[:4] != b"\x03\x02\x23\x07":
        raise RuntimeError("shader compiler did not produce SPIR-V")
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    publish(directory / (name + "_spv.h"), "#include <stdint.h>\nstatic const uint32_t " + name + "_spv[] = {\n" +
            "\n".join("  " + ", ".join(f"0x{word:08x}" for word in words[index:index + 8]) + ","
                      for index in range(0, len(words), 8)) + "\n};\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("i386", "x86_64"), required=True)
    builders = {"libcxx": build_libcxx, "llvm": build_llvm, "mesa": build_mesa, "shaders": build_shaders}
    parser.add_argument("--component", choices=builders, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--shader", type=Path, help="GLSL source relative to apps/ (shaders component)")
    arguments = parser.parse_args()
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    output = APPS / "out" / ("x86_64" if arguments.arch == "x86_64" else "")
    (output / "mesa").mkdir(parents=True, exist_ok=True)
    if arguments.component == "shaders":
        if arguments.shader is None:
            parser.error("--component shaders requires --shader")
        build_shaders(arguments.arch, output, APPS / arguments.shader)
        return
    with (output / "mesa/.build-lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        builder = builders[arguments.component]
        builder(arguments.arch, output, arguments.jobs)


if __name__ == "__main__":
    main()
