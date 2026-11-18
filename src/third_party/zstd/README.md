# zstd decoder (vendored)

The decompression half of [Zstandard](https://github.com/facebook/zstd) **v1.5.7** (commit `f8745da6ff1ad1e7bab384bd1f9d742439278e99`), unchanged: `lib/zstd.h`, `lib/zstd_errors.h`, the `lib/common` files the decoder needs, and `lib/decompress` without its x86 assembly. BSD licence (`LICENSE`).

Slingshot uses it to read the game's and the mods' resource size tables at each start (`dm/slingshot/size_table.c`). It writes its merged table back as a zstd frame of raw blocks, which needs no encoder.

Built with `-DZSTD_DISABLE_ASM` (the Makefile's rule for `dm/slingshot/third_party`).
