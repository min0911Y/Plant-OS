#!/usr/bin/env python3
"""Build the pinned LWJGL modules against the Plant OS ABI."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from sources import ROOT, Sources, digest, publish


APPS = ROOT / "apps"
PORT = APPS / "lwjgl"



def find_tool(name, candidates=()):
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    found = shutil.which(name)
    if found:
        return found
    raise RuntimeError(f"missing host tool: {name}")


def target_jni_include():
    candidates = [
        os.environ.get("PLANT_OPENJDK_INCLUDE"),
        APPS / "out/x86_64/openjdk/images/jdk/include",
        APPS / "out/sources/openjdk-17-17.0.19+10/src/java.base/share/native/include",
    ]
    for candidate in candidates:
        if not candidate:
            continue
        directory = Path(candidate)
        if not (directory / "jni.h").is_file():
            continue
        if (directory / "jni_md.h").is_file():
            return (directory,)
        for machine_directory in ("bsd", "unix", "linux"):
            if (directory / machine_directory / "jni_md.h").is_file():
                return directory, directory / machine_directory
        source_machine_directory = directory.parents[2] / "unix/native/include"
        if (source_machine_directory / "jni_md.h").is_file():
            return directory, source_machine_directory
    raise RuntimeError(
        "target OpenJDK 17 JNI headers are missing; build the Plant JDK first "
        "or set PLANT_OPENJDK_INCLUDE"
    )


def javac_tool():
    return find_tool("javac", [
        os.environ.get("JAVAC"),
        APPS / "out/host/openjdk/jdk-17.0.2/bin/javac",
    ])


def java_tool(name):
    return find_tool(name, [
        APPS / "out/host/openjdk/jdk-17.0.2/bin" / name,
    ])


def java_sources(source, module, extra=(), exclude=()):
    directories = [source / "modules/lwjgl" / module / "src/main/java",
                   source / "modules/lwjgl" / module / "src/generated/java"]
    result = []
    for directory in directories:
        if directory.is_dir():
            result.extend(directory.rglob("*.java"))
    result.extend(extra)
    excluded = set(exclude)
    return sorted(path for path in result
                  if path.name not in excluded and path.name != "module-info.java")


def compile_java_module(source, module, destination, classpath, exclude=()):
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    sources = java_sources(source, module, exclude=exclude)
    if not sources:
        raise RuntimeError(f"no Java sources for {module}")
    argfile = destination.parent / f"{module}-sources.txt"
    argfile.write_text("\n".join(str(path) for path in sources) + "\n")
    command = [javac_tool(), "-encoding", "UTF-8", "-source", "8", "-target", "8",
               "-Xlint:-options", "-XDignore.symbol.file", "-cp", os.pathsep.join(classpath),
               "-d", str(destination), "@" + str(argfile)]
    subprocess.run(command, check=True)


def make_jar(jar, classes):
    jar.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([java_tool("jar"), "--create", "--file", str(jar), "-C", str(classes), "."],
                   check=True)


def compile_smoke(output, jar_dir):
    classes = output / "classes"
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir(parents=True)
    jars = [jar_dir / name for name in (
        "lwjgl-3.3.6.jar", "lwjgl-glfw-3.3.6.jar", "lwjgl-opengl-3.3.6.jar",
        "lwjgl-stb-3.3.6.jar", "lwjgl-openal-3.3.6.jar", "jspecify-1.0.0.jar")]
    subprocess.run([
        javac_tool(), "-source", "8", "-target", "8", "-Xlint:-options",
        "-cp", os.pathsep.join(map(str, jars)), "-d", str(classes),
        str(PORT / "LwjglSmoke.java"),
    ], check=True)


def native_flags(source, jni, ffi, extra_include=()):
    gcc_include = subprocess.check_output(
        [find_tool("gcc"), "-print-file-name=include"], text=True
    ).strip()
    return [
        find_tool("gcc"), "-m64", "-mcmodel=small", "-mno-red-zone", "-mno-mmx",
        "-msse2", "-mfpmath=sse", "-mlong-double-64", "-std=gnu17",
        "-nostdinc", "-isystem", gcc_include, "-I", str(APPS / "include"),
        "-I", str(PORT / "include"), "-I", str(source / "modules/lwjgl/core/src/main/c"),
        "-I", str(source / "modules/lwjgl/core/src/main/c/libffi"),
        "-I", str(source / "modules/lwjgl/core/src/main/c/libffi/x86"),
        "-I", str(ffi / "include"), "-I", str(ffi / "src"), "-I", str(ffi / "src/x86"),
        *sum((["-I", str(path)] for path in jni), []),
        "-DLWJGL_PLANTOS", "-DPLANT_ARCH_X86_64", "-D__plantos__", "-DX86_64",
        "-D__USE_ISOC11",
        "-DFFI_STATIC_BUILD", "-DNDEBUG", "-U__linux__", "-U__linux", "-Ulinux",
        "-U__unix__", "-U__unix", "-Uunix", "-fPIC", "-ffreestanding", "-fno-builtin",
        "-fno-stack-protector", "-fno-asynchronous-unwind-tables", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-Wno-sign-compare", "-Wno-unused-function",
        "-include", str(APPS / "include/alloca.h"),
        *sum((["-I", str(path)] for path in extra_include), []),
    ]


def compile_native(source, ffi, jni, build, group, paths, extra_include=()):
    objects = []
    for path in paths:
        path = Path(path)
        object = build / "objects" / group / (path.stem + ".o")
        object.parent.mkdir(parents=True, exist_ok=True)
        command = native_flags(source, jni, ffi, extra_include)
        command += ["-c", str(path), "-o", str(object)]
        subprocess.run(command, check=True)
        objects.append(object)
    return objects


def link_shared(output, name, objects, runtime):
    destination = output / "native" / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    linker = find_tool("ld")
    command = [linker, "-m", "elf_x86_64", "-z", "max-page-size=4096",
               "-z", "noexecstack", "-z", "relro", "-z", "now", "-z", "text",
               "-shared", "--no-undefined", "--hash-style=both", "-soname", name,
               "-o", str(destination), *map(str, objects), str(runtime / "dynamic/libp/dso.o"),
               str(runtime / "lib/libp.so")]
    subprocess.run(command, check=True)
    return destination


def build_stb_native(source, output):
    """Build an application's pinned STB JNI against the shared Plant core ABI."""
    dependencies = Sources(PORT)
    core = dependencies["lwjgl"].prepare()
    ffi = dependencies["libffi"].prepare()
    jni = target_jni_include()
    runtime = APPS / "out/x86_64"
    state = json.dumps({
        "source": digest(source / ".plant-source-sha256"),
        "core": digest(core / ".plant-source-sha256"),
        "script": digest(Path(__file__)),
        "config": digest(PORT / "include/PlantOSConfig.h"),
        "runtime": digest(runtime / "lib/libp.so"),
        "jni": [digest(jni[0] / "jni.h"), digest(jni[-1] / "jni_md.h")],
    }, sort_keys=True) + "\n"
    stamp = output / ".plant-build"
    library = output / "native/liblwjgl_stb.so"
    if stamp.exists() and stamp.read_text() == state and library.exists():
        return library
    sources = sorted((source / "modules/lwjgl/stb/src/generated/c").glob("*.c"))
    objects = compile_native(core, ffi, jni, output, "stb", sources,
                             [source / "modules/lwjgl/stb/src/main/c"])
    link_shared(output, "liblwjgl_stb.so", objects, runtime)
    publish(stamp, state)
    return library


def build(arch, output, jobs):
    if arch != "x86_64":
        raise RuntimeError("LWJGL currently supports x86_64 only")
    output.mkdir(parents=True, exist_ok=True)

    dependencies = Sources(PORT)
    lwjgl = dependencies["lwjgl"].prepare()
    ffi = dependencies["libffi"].prepare()
    jspecify = dependencies["jspecify"].prepare()
    jni = target_jni_include()
    runtime = output.parent
    required_runtime = [runtime / "lib/libp.so", runtime / "lib/libglfw.so",
                        runtime / "lib/libEGL.so", runtime / "lib/libGL.so"]
    if any(not path.is_file() for path in required_runtime):
        missing = ", ".join(str(path) for path in required_runtime if not path.is_file())
        raise RuntimeError(f"native graphics/runtime prerequisites are missing: {missing}")

    artifact_paths = [
        "jar/lwjgl-3.3.6.jar", "jar/lwjgl-glfw-3.3.6.jar",
        "jar/lwjgl-opengl-3.3.6.jar", "jar/lwjgl-stb-3.3.6.jar",
        "jar/lwjgl-openal-3.3.6.jar",
        "jar/jspecify-1.0.0.jar", "native/liblwjgl.so",
        "native/liblwjgl_opengl.so", "native/liblwjgl_stb.so",
        "classes/LwjglSmoke.class", "run-lwjgl.lua",
    ]
    state = {
        "arch": arch,
        "script": digest(Path(__file__)),
        "lwjgl": (lwjgl / ".plant-source-sha256").read_text(),
        "libffi": digest(ffi / ".plant-source-sha256"),
        "jspecify": digest(jspecify),
        "jni": {str(path): digest(path / name) for path, name in
                [(jni[0], "jni.h"), (jni[-1], "jni_md.h")]},
        "runtime": {str(path.relative_to(runtime)): digest(path) for path in required_runtime},
        "port": {str(path.relative_to(ROOT)): digest(path)
                  for path in [PORT / "sources.json", PORT / "include/PlantOSConfig.h",
                               PORT / "include/fficonfig.h", PORT / "plantos_closures.c",
                               PORT / "plantos_dynamic_loader.c", PORT / "plantos_ffi.c",
                               *sorted((PORT / "patches/lwjgl").glob("*.patch"))]},
        "smoke": {str(path.relative_to(ROOT)): digest(path)
                  for path in [PORT / "LwjglSmoke.java", PORT / "run-lwjgl.lua"]},
        "artifacts": artifact_paths,
    }
    state_path = output / ".plant-build"
    if state_path.exists() and state_path.read_text() == json.dumps(state, sort_keys=True) + "\n" and \
            all((output / path).is_file() for path in artifact_paths):
        return

    work = output / ".build"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    java_build = work / "java"
    core_classes = java_build / "core"
    compile_java_module(lwjgl, "core", core_classes, [str(jspecify)])
    opengl_classes = java_build / "opengl"
    compile_java_module(lwjgl, "opengl", opengl_classes, [str(jspecify), str(core_classes)])
    glfw_classes = java_build / "glfw"
    compile_java_module(lwjgl, "glfw", glfw_classes,
                        [str(jspecify), str(core_classes), str(opengl_classes)],
                        exclude=("GLFWNativeEGL.java", "GLFWVulkan.java"))
    stb_classes = java_build / "stb"
    compile_java_module(lwjgl, "stb", stb_classes, [str(jspecify), str(core_classes)])
    openal_classes = java_build / "openal"
    compile_java_module(lwjgl, "openal", openal_classes, [str(jspecify), str(core_classes)])

    jar_dir = output / "jar"
    make_jar(jar_dir / "lwjgl-3.3.6.jar", core_classes)
    make_jar(jar_dir / "lwjgl-glfw-3.3.6.jar", glfw_classes)
    make_jar(jar_dir / "lwjgl-opengl-3.3.6.jar", opengl_classes)
    make_jar(jar_dir / "lwjgl-stb-3.3.6.jar", stb_classes)
    make_jar(jar_dir / "lwjgl-openal-3.3.6.jar", openal_classes)
    shutil.copyfile(jspecify, jar_dir / "jspecify-1.0.0.jar")
    compile_smoke(output, jar_dir)
    shutil.copyfile(PORT / "run-lwjgl.lua", output / "run-lwjgl.lua")

    core_generated = sorted((lwjgl / "modules/lwjgl/core/src/generated/c").glob("*.c"))
    core_sources = [lwjgl / "modules/lwjgl/core/src/main/c/common_tools.c",
                    lwjgl / "modules/lwjgl/core/src/main/c/org_lwjgl_system_Callback.c",
                    lwjgl / "modules/lwjgl/core/src/main/c/org_lwjgl_system_MemoryUtil.c",
                    lwjgl / "modules/lwjgl/core/src/main/c/org_lwjgl_system_SharedLibraryUtil.c",
                    lwjgl / "modules/lwjgl/core/src/main/c/org_lwjgl_system_ThreadLocalUtil.c",
                    PORT / "plantos_closures.c", PORT / "plantos_dynamic_loader.c", PORT / "plantos_ffi.c",
                    *core_generated]
    ffi_sources = [ffi / "src/prep_cif.c", ffi / "src/types.c", ffi / "src/raw_api.c",
                   ffi / "src/java_raw_api.c", ffi / "src/x86/ffi64.c",
                   ffi / "src/x86/ffiw64.c", ffi / "src/x86/unix64.S",
                   ffi / "src/x86/win64.S"]
    ffi_objects = compile_native(lwjgl, ffi, jni, work, "libffi", ffi_sources)
    core_objects = compile_native(lwjgl, ffi, jni, work, "core", core_sources)
    opengl_sources = [path for path in sorted(
        (lwjgl / "modules/lwjgl/opengl/src/generated/c").glob("*.c"))
                      if path.name != "org_lwjgl_opengl_WGL.c"]
    opengl_objects = compile_native(lwjgl, ffi, jni, work, "opengl", opengl_sources,
                                    [lwjgl / "modules/lwjgl/opengl/src/main/c"])
    stb_sources = sorted((lwjgl / "modules/lwjgl/stb/src/generated/c").glob("*.c"))
    stb_objects = compile_native(lwjgl, ffi, jni, work, "stb", stb_sources,
                                 [lwjgl / "modules/lwjgl/stb/src/main/c"])
    link_shared(output, "liblwjgl.so", [*core_objects, *ffi_objects], runtime)
    link_shared(output, "liblwjgl_opengl.so", opengl_objects, runtime)
    link_shared(output, "liblwjgl_stb.so", stb_objects, runtime)
    publish(state_path, json.dumps(state, sort_keys=True) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    build(args.arch, args.output.resolve(), args.jobs)


if __name__ == "__main__":
    main()
