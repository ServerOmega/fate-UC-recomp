# Building it

`build.sh` does all of this. This document explains what it does and what to do when a step
fails, so it can be done by hand.

## What you need

- **A disc image of Fate/unlimited codes** you own (`SLPM-55108`). Nothing here contains game
  data, and nothing here will run without your disc.
- **Git, CMake, Ninja**, and a **C++20 compiler**. Upstream's vector code needs Clang, so
  `clang++` is the safe choice; the scripts use it by default.
- **Windows**: MSVC Build Tools (the STL and the Windows SDK), or LLVM-MinGW. The scripts
  here assume the MinGW-style toolchain that upstream's `scripts/cxx.sh` finds.
- **Linux**: `SDL2-devel`, `libX11-devel`, `vulkan-loader` (or the software fallback).

## The five steps

### 1. Clone the runtime

```bash
git clone https://github.com/exoteric33/ps2-native-recomp.git .local/upstream
cd .local/upstream
git checkout 5d1ebbcf133cb3c31df75a294dbe38e30734c1ce
```

That commit is the one the patches were written against. A later upstream will probably still
apply them, but `build.sh` reports a clear failure instead of applying a half-matching patch.

### 2. Apply the patches

```bash
cd .local/upstream
for p in ../../patches/*.patch; do git apply "$p"; done
```

Eleven files. Each patch is a full-context diff of one file, so they can be applied in any
order and a file that already has yours is skipped by `build.sh`.

What each one is for:

| Patch | Change |
|---|---|
| `CMakeLists.txt.patch` | compiles `r3000_native.cpp` and `gen_iop.cpp` |
| `runtime_src_iop_iop.hpp.patch` | registers the native IOP table |
| `runtime_src_iop_kernel.cpp.patch` | HLE entry point for the native path |
| `runtime_src_system.cpp.patch` | `PS2_QOS` through the tuned defaults |
| `runtime_src_gs_gpu_gpu.cpp.patch` | tuned defaults for power, presentation and sharpening; `gl` backend |
| `runtime_src_iop_r3000_native.cpp` | **the bridge** (copied, not patched — it is new) |
| `host_stretch.cpp.patch` | `kEnter` back to 0.995 (see below) |
| `host_window.cpp.patch` | frame log fields |
| `host_keymap.cpp.patch` | mentions the tuned defaults in the generated `keys.ini` |
| `host_quickmenu.cpp.patch` | says when a setting is locked by an environment variable |
| `scripts_cxx.sh.patch`, `scripts_port.sh.patch` | toolchain paths for this machine |

### 3. Copy our sources

```bash
cp -r ../../recomp/. recomp/
cp -r ../../runtime/. runtime/
cp ../../scripts/frame_report.py scripts/
```

### 4. Generate the native IOP code

The gerador reads the IRX modules. They are not loose files on the disc: the game keeps them
inside `MODULES/IOPRP310.IMG`, which is an IOP executable image with the modules embedded.

```bash
clang++ -std=gnu++20 -I. -Iruntime/include -Iruntime/src -O2 \
    -o ../gen_iop recomp/r3000_gen.cpp recomp/gen_iop.cpp recomp/elf.cpp recomp/r3000.cpp
../gen_iop ../iop-modules ../generated-iop
```

`iop-modules/` must hold the extracted IRX files. Extract them from `IOPRP310.IMG` on your own
disc — the image is a sequence of IRX images with a header; `docs/IOP-MODULES.md` records the
layout that was found. **Do not commit the extracted modules**: they are game data.

The generator prints what it produced. A healthy run on this game looks like:

```
20 modules, 100.00% dispatch, 0 escapes
```

### 5. Build

```bash
cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C ../build ps2run
```

## When something fails

**A patch does not apply.** The upstream moved past the pinned commit. Either check out the
pinned one, or rebase the patch by hand: the changes are small and commented.

**The generator says fewer than 20 modules.** `iop-modules/` is incomplete. Re-extract from
`IOPRP310.IMG`.

**It links but the game does not boot.** Check that `games/fate/disc.iso` is the right
serial (`SLPM-55108`). The log's first lines name the disc and its sector count.

**`PS2_GPU_BACKEND=vulkan` is slower.** Expected — measured 233 ms worst frame against 17 ms
on Direct3D 12.

## A note on `host/stretch.cpp`

The audio stretcher's entry threshold (`kEnter`) was tried at 0.99 and it made things worse:
stretched sound rose from 4% of the run to 32%, because a looser threshold lets the stretcher
stay in longer. It is back at 0.995 and the reason is in the comment. The real fix was making
the game run at 60 Hz, after which the stretcher has no work.
