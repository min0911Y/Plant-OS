# SDL3 native source subset

Upstream: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16

Archive: `SDL3-3.4.16.tar.gz`

SHA256: `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68`

The native source selection is maintained in `sources.mk`; the configuration
and platform implementation are in `config/` and `src/{video,timer,time}/plos/`.
`LICENSE.txt` and the notices in upstream sources are retained.

The Vulkan port restores `src/render/vulkan` and
`src/video/khronos/{vulkan,vk_video}` from this exact release, including the
SPIR-V headers distributed by SDL and their HLSL sources. These files are not
host runtime libraries. Native Vulkan/window glue stays in the PLOS backend
and `apps/mesa`, with details in `doc/lavapipe.md`.
