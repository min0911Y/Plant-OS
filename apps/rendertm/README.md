# RenderTM for Plant OS

Terminal renderer from https://github.com/xiaoyi1212/RenderTM,
revision `68fddea51e0d4cfd55f7f1091005f7dd4b5fff72`.

The renderer, terrain, lighting, input parser and C++ module structure are retained.
The native port replaces POSIX terminal setup, polling, size queries and output
with Plant OS calls. It uses the existing `term.bin` terminal and has no SDL dependency.
The upstream snapshot does not contain a license file.

Build with `make -C apps/rendertm ARCH=x86_64` or `ARCH=i386` from the repository root.
Run `rendertm.bin` inside the GUI terminal, or launch
`term.bin rendertm.bin` from a shell with the GUI service running.

The independent SDL3 frontend is `renderhd.bin`, built with
`make -C apps/renderhd ARCH=x86_64` (or `ARCH=i386`). It shares this renderer and
defaults to a 480x300 window. `--width`, `--height` and `--workers` select the
resolution and worker count; the terminal executable remains independent of SDL.

WASD/arrows move, R/F rise/fall, the mouse controls look, P pauses the sun/moon,
G toggles global illumination, O toggles ambient occlusion, and Q exits.

See [the port documentation](../../doc/rendertm.md) for architecture, dependencies
and native regression commands. The original Catch2 test sources are retained
as upstream references; the native regression uses real terminal pixels and input.
