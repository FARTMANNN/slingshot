# Third-party code and licences

## nxnative (MIT)

Slingshot is made for [nxnative](https://github.com/seedds/nxnative) and changes some of its files: `patches/nxnative.patch`, applied by `tools/install.sh`. Those changes are under nxnative's licence:

```
MIT License

Copyright (c) 2026 seedds

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Squadala! (GPL-3.0)

[Squadala!](https://github.com/daurosk/oot-remake-multiplayer) by daurosk and its contributors. It is not part of this repository: `tools/squadala_setup.sh` clones it at a pinned release and builds it, and Slingshot loads the result as a separate library. `patches/squadala-macos.patch` changes its source, so that file is GPL-3.0, as Squadala! is.

## OOTRR (GPL-3.0-or-later)

[OOTRR](https://github.com/REALAZK/ootrr) by REALAZK. It is not part of this repository: `tools/squadala_setup.sh` clones and builds it, and Squadala!'s module runs it as its own program.

## zstd (BSD)

`src/third_party/zstd` holds the decoding half of [zstd](https://github.com/facebook/zstd) 1.5.7, unchanged, under its BSD licence (`src/third_party/zstd/LICENSE`).

## Opus (BSD)

Voice chat loads [libopus](https://opus-codec.org) at run time, the copy Homebrew installs. libopus is not part of this repository.
