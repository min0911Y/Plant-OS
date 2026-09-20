# LWJGL 3.3.6 source and port

This directory builds the core, GLFW, OpenGL, STB and OpenAL modules from the
upstream LWJGL 3.3.6 source archive.  The archive URL and SHA-256 are in
`sources.json`; the build cache is `apps/out/sources/` and is never copied to
the target image.

LWJGL 3.3.6 is the last 3.3 release at this baseline.  Its release notes list
GLFW 3.4, matching `apps/glfw`, and the upstream build supports Java 8 and
newer, so the existing OpenJDK 17 is within its supported range.  LWJGL 3.4.x
is not selected because its GLFW dependency moved beyond the native GLFW 3.4
port.

The upstream source is licensed under the LWJGL license in its `LICENSE.md`.
The embedded generated bindings carry the upstream notices.  The callback
allocator uses libffi 3.4.4 under its own license; its source and checksum are
also fixed in `sources.json`.  JSpecify 1.0.0 is a compile-time annotation
dependency and is not a target native library.

Plant OS changes are kept as patches applied to the source cache:

- add a first-class `PLANTOS` platform without changing `os.name` to Linux;
- route library loading through the existing Plant ELF loader and path-list
  separator;
- use the target JNI headers and Plant freestanding headers;
- allocate libffi closures with separate RW and RX aliases.

Only x86_64 is supported.  The target library directory contains the three
LWJGL JNI libraries; the existing system `libglfw.so`, `libEGL.so` and
`libGL.so` remain the single shared graphics implementation.

OpenAL uses core JNI function pointers and the native system `libopenal.so`.
Minecraft 1.20.1 separately pins STB 3.3.1 in `apps/minecraft/sources.json`;
its application-private JNI is built against the same Plant core headers and
allocator ABI, without changing the system STB 3.3.6 API.
