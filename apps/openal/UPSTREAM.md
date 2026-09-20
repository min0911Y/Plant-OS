# OpenAL Soft

- Source: https://openal-soft.org/openal-releases/openal-soft-1.19.1.tar.bz2
- SHA-256: `5c2f87ff5188b95e0dc4769719a9d89ce435b8322b4478b95dd4b427fe84b2e9`
- License: LGPL 2 or later; see [COPYING](COPYING) and upstream source notices.

This C implementation runs on Plant's freestanding runtime without requiring
C++ exception support. It includes the upstream mixer, effects, resamplers,
SSE/SSE2 paths, null playback and loopback rendering. No host audio library or
physical audio device driver is included. Null playback consumes audio without
sending it to a speaker; loopback renders real PCM into application buffers.

`config.h` describes the native x86_64 ABI. `build-openal.py` builds the upstream
resampler table generator on the host and the library with Plant headers and
libp. The patch adds missing extern declarations for modern GCC. Sources and
patches are prepared and verified through `sources.py`.
