# liballoc

This directory vendors the official `liballoc-i686.a` asset from:

https://github.com/plos-clan/liballoc/releases/tag/release

- Upstream commit: `6fedb1bbe7b13995f3c37b207a1a325ef1a4d287`
- Asset SHA-256: `7f16b250dcd01426990fc42c721c43a30651affae58f6147f6032025c1be943b`
- Architecture/ABI: ELF32 i386, freestanding C ABI
- Upstream allocator: `talc 4.4.3` with `spin 0.10.0`
- License: MIT; see `LICENSE`

The release archive is kept byte-for-byte unchanged. `kernel/Makefile` creates
an ignored build copy with all public exports moved into the `liballoc_*`
private namespace; the
kernel-facing `malloc`, `free`, `realloc`, `kmalloc`, and KASAN semantics remain
owned by `kernel/dos/mm/heap.c`.

The x86_64 backend uses the same release's `liballoc-x86_64.a` (ELF64 AMD64).
SHA-256: `2137fa65410bfecc22371769f3557e8f342216254afe67ad72e76117d1446d08`.
Both architectures build separate archives with private symbol names.
