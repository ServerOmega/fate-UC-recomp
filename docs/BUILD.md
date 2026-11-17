# Building it

`build.sh` does all of this. This document explains what it does and what to do when a step
fails, so it can be done by hand.

## What you need

- **A disc image of Fate/unlimited codes** you own (`SLPM-55108`). Nothing here contains game
  data, and nothing here will run without your disc. `./build.sh --runtime-only` is the one
  exception: it compiles the runner with no game in it, so a machine can be checked before
  the disc is on it.
- **Git, CMake, Ninja, Python 3, curl**, and **Clang 17 or newer**. Upstream's vector code
  uses clang builtins (`__builtin_elementwise_min`, `__builtin_reduce_or`) and the root
  `CMakeLists.txt` stops if the compiler is not clang. GCC will not build it.
- **`CXX`**, when you already know which clang to use. It wins over the search below.

`build.sh` checks this list and prints the missing names with install lines for Arch,
Debian/Ubuntu and Fedora (and `winget` on Windows).

### Windows

Git Bash. The compiler search, when `CXX` is unset:

1. `clang++` / `clang` on `PATH` (newest `clang++-N` first, the same order as upstream's
   `scripts/cxx.sh`)
2. LLVM-MinGW installed by WinGet, under
   `%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW*\llvm-mingw-*\bin\`
   (any version, the current user — no path baked in)
3. `C:\Program Files\LLVM\bin` and `C:\llvm-mingw\bin`

```
winget install Git.Git Kitware.CMake Ninja-build.Ninja Python.Python.3 MartinStorsjo.LLVM-MinGW.UCRT
```

SDL2, wgpu-native and the DXC shader compiler (`dxcompiler.dll`, `dxil.dll`) are downloaded
by upstream's `scripts/setup.sh`. DXC is Direct3D 12 and is fetched only on Windows.

### Linux

The pinned runtime (`ps2-native-recomp` at `5d1ebbc`) links **SDL2** for the window and
statically links **wgpu-native v29.0.1.1**. That library's link line is `dl`, `pthread` and
`libm`. At run time it `dlopen`s `libvulkan.so.1`, and for the surface `libwayland-egl.so.1`
(KDE Plasma on Wayland) or libX11. `SDL_syswm.h` includes `X11/Xlib.h`, so the X11 headers
are a compile dependency of the official SDL2 packages. There is no `dxcompiler` on Linux.

| | Arch Linux | Debian / Ubuntu | Fedora |
|---|---|---|---|
| toolchain | `clang cmake ninja git python curl` | `clang cmake ninja-build git python3 curl` | `clang cmake ninja-build git python3 curl` |
| window | `sdl2 libx11 wayland` | `libsdl2-dev libx11-dev libwayland-dev` | `SDL2-devel libX11-devel wayland-devel` |
| Vulkan loader | `vulkan-icd-loader` | `libvulkan1` | `vulkan-loader` |
| a driver | `vulkan-radeon`, `vulkan-intel`, `nvidia-utils`, or `vulkan-swrast` | `mesa-vulkan-drivers`, or the NVIDIA package | `mesa-vulkan-drivers`, or the NVIDIA package |

```bash
# Arch
sudo pacman -S --needed clang cmake ninja git python curl sdl2 libx11 wayland vulkan-icd-loader
# Debian / Ubuntu
sudo apt install clang cmake ninja-build git python3 curl libsdl2-dev libx11-dev libwayland-dev libvulkan1
# Fedora
sudo dnf install clang cmake ninja-build git python3 curl SDL2-devel libX11-devel wayland-devel vulkan-loader
```

A driver is what draws the window. With no ICD json under `/usr/share/vulkan/icd.d` the
binary still links, and `build.sh` says so.

If clang is present but `<format>` is missing, it is bound to a libstdc++ older than the
header (on Ubuntu, clang can select a newer GCC than the one whose headers are installed).
Install that GCC's `libstdc++-N-dev`, or libc++ (`libc++` on Arch, `libc++-dev` and
`libc++abi-dev` on Debian, `libcxx-devel` on Fedora). The probe tries `-stdlib=libc++` on
its own.

### Where the program ends up

`build.sh` copies `config/keys.ini` and the launcher for this OS into the build folder, and
links the disc image you passed so the launcher can see it:

| | Windows | Linux |
|---|---|---|
| runner | `ps2run.exe` | `ps2run` |
| launcher | `play.bat` | `play.sh` |
| folder, full build | `.local/upstream/build/native-fate/` | the same |
| folder, `--runtime-only` | `.local/upstream/build/host-only/` | the same |

`keys.ini` is also copied to `games/fate/` on a full build, which is the folder of
`--memcard` in the manual command. An existing `keys.ini` is left alone.

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

Twelve patch files. Each patch is a full-context diff of one file, so they can be applied in any
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
| `scripts_cxx.sh.patch`, `scripts_port.sh.patch`, `scripts_setup.sh.patch` | Windows-only `cygpath` on paths handed to native tools |

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

`build.sh` does not run this cmake for a disc. It runs `scripts/port.sh`, which is what
creates the `ps2run` target (`-DPS2_GENERATED` points at the translated game). The binary
is `ps2run.exe` on Windows and `ps2run` on Linux, in `build/native-fate/`.

A host-only build (no disc) is `./build.sh --runtime-only`. It still configures cmake, with
a two-function stub standing in for the generated game, and writes `build/host-only/ps2run`.
That binary links and opens no game.

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
