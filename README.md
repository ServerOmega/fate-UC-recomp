# fate-UC-recomp

Static recompilation of **Fate/unlimited codes** (PS2, `SLPM-55108`) into a native PC
program. The game's Emotion Engine, both vector units and its I/O processor are translated
to C++ and compiled; the rest of the console is imitated by a runtime.

This repository holds **only what is specific to this game**: the native IOP dispatch, the
tuned run settings, the patches they need, and the documentation. It is not a fork and it
does not vendor the runtime. The runtime is [ps2-native-recomp] by Samir Nuri (MIT), cloned
at a pinned commit by `build.sh` — see [Built on](#built-on).

**No game data is here.** No disc image, no extracted assets, no translated game source, no
save, no screenshot. You bring your own disc; `build.sh` turns it into a program.

---

## Quick start

Windows (Git Bash) or Linux. What to install is in [BUILD.md](docs/BUILD.md): clang 17+,
CMake, Ninja, Git, Python 3. On Linux also SDL2, libX11, Wayland and a Vulkan loader plus a
driver. `build.sh` names the missing packages for Arch, Debian/Ubuntu and Fedora.

```bash
git clone https://github.com/ohackercego/fate-UC-recomp.git
cd fate-UC-recomp
./build.sh "/path/to/Fate unlimited codes.iso"
```

The build fills `.local/upstream/build/native-fate/` with the runner, a launcher and
`keys.ini`. Put the disc image next to the launcher (the build links the one you passed) and
start it:

- **Windows:** double-click `.local/upstream/build/native-fate/play.bat`
- **Linux:** `./.local/upstream/build/native-fate/play.sh`

`play.bat` and `play.sh` take the same argument if the image is somewhere else:
`play.sh "/path/to/another copy.iso"`.

Or run the binary yourself. On Windows:

```bash
./.local/upstream/build/native-fate/ps2run.exe --disc .local/upstream/games/fate/disc.iso \
    --memcard .local/upstream/games/fate/card.ps2 \
    --game-dir .local/upstream/games/fate --window
```

On Linux the program in that folder is `ps2run`, and the same arguments apply.

**Controls:**

- **Arrows** — move. They drive both the d-pad and the left stick, because the menus
  read the stick and the fighting game reads the d-pad.
- **A S D F** — square, triangle, circle, cross
- **Z X C V** — L1, L2, R1, R2
- **Enter** — start · **Tab** — select
- **Space** — cross (cancel)
- **F2** — quick save · **F4** — quick load
- **F1** — quick menu · **F11** — full screen

This is a Japanese game: **circle (D) confirms, cross (F or Space) goes back.**

The keys ship in `config/keys.ini`. `build.sh` copies that file next to the runner, and into
`games/fate/`, when it is not already there. Change it in the copy, or from the F1 menu.

On Linux the picture is Vulkan. Direct3D 12 and `dxcompiler` are Windows-only, and `PS2_QOS`
is applied only on Windows. The other settings below are read on both.

---

## What this project adds

Everything below is measured on a Ryzen 5 5500U with its integrated Radeon, Direct3D 12,
playing this game. The measurements are in [MEASUREMENTS.md](docs/MEASUREMENTS.md).

### 1. Native IOP dispatch — 100.00%, 0 escapes

The IOP is the PS2's second processor (an R3000). On the console it runs the game's own IRX
modules: the sound driver, the disc streaming code, the controller and memory-card drivers.
Upstream interprets them. Here they are translated to C++ and compiled.

- `recomp/r3000_gen.cpp` + `recomp/gen_iop.cpp` walk each IRX, follow its control flow and
  emit C++ per function.
- `runtime/src/iop/r3000_native.cpp` is the bridge: HLE, syscall, breakpoint, module lookup
  **by name** (not by load order), delay-slot handling, and the cycle budget charged by each
  entry's real cost instead of one per instruction.
- On Fate/unlimited codes: **20 modules, 100.00% dispatch, 0 escapes.**

A differential test compares state instruction by instruction between the native build and a
build with an interpreted IOP. That is how a translation bug is found — not by looking at the
game's output.

### 2. Tuned run defaults, compiled into the binary

`runtime/include/ps2/tuned_defaults.hpp` carries the settings that measured best, so a plain
`ps2run.exe` comes up with them instead of the upstream defaults.

| Setting | Effect | Why |
|---|---|---|
| `PS2_GPU_LOW_POWER=1` | low-power adapter preference | stops the GPU climbing its clock and then throttling: worst frame 64.5 → 33.9 ms |
| `PS2_QOS=1` | opt out of Windows EcoQoS | Windows parks the emulation threads on the small cores otherwise |
| `PS2_PRESENT_MODE=immediate` | no vsync wait | 46.4 → 57.7 Hz measured |
| `PS2_SHARP_2D=0` | no 2D sharpening filter | **half the CPU**: 204% → 102% |

**In a real fight:** 46.37 Hz with 60.6% of the audio stretched → **57.69 Hz with 7.8%**.

Each one is overridden by the environment variable named beside it, and `PS2_TUNED=0` gives
back the upstream behaviour exactly.

### 3. A fourth GPU backend, for testing

`PS2_GPU_BACKEND` now also accepts `gl` / `opengl` / `gles`. wgpu does find the AMD OpenGL
driver (4.6.0 Compatibility Profile) and then hits **its own issue #5299**, "Could not lock
adapter context. This is most-likely a deadlock", open since February 2024. The path is only
taken when asked for by name, never as a fallback — if wgpu fixes it, this is ready.

### 4. Frame-time diagnosis

`scripts/frame_report.py` reads `PS2_FRAME_LOG` and separates what each slow frame waited
for: emulation CPU, the window, or the sound.

It is how the real bottleneck was found:

```
frame 3642:  total 646.96 ms
             waiting for the window  636.84 ms  (98.4%)
             emulation CPU            46.88 ms
             waiting for audio         0.01 ms
             shapes drawn             42,130
```

A special move sends **42 thousand shapes in one frame**. Of the bad frames, **81% have more
than 20 thousand shapes**. The time is lost in presentation, not in emulation — and the
`interpreted` column stayed at `0` across 19,000 consecutive combat frames.

---

## What is native and what is imitated

**The game is 100% native.** Every instruction the game executes was translated and compiled;
the `interpreted` column of the frame log stayed at `0` over 19,000 frames of combat. The
generated game code is about 1,830,000 lines in 205 files.

**The console is imitated**, in about 32,300 lines of C++:

| Component | Lines | Role |
|---|---|---|
| GS (graphics) | 18,179 | 4 MiB of memory on the GPU, compute shaders |
| IOP (second processor) | 9,159 | sound, input, disc |
| IPU | 1,112 | MPEG-2 video |
| PS2 kernel | 1,111 | syscalls |
| SPU2 (sound) | 896 | voice synthesis, mixed in software |
| VIF1 | 772 | data transfer to the vector unit |
| DMAC | 590 | block memory moves |
| CD/DVD | 330 | ISO reading |
| COP | 153 | coprocessor registers |

The graphics chip is 56% of everything imitated, and it is where the remaining cost lives.

---

## What was tried and does not work

Measured and discarded, with numbers:

| Path | Result |
|---|---|
| Vulkan | worst frame 233 ms |
| OpenGL | does not start (wgpu #5299) |
| CPU rasterizer | 21 fps |
| `PS2_GPU_HW_INDEP_MAX=all` | 81 bad frames in 19,000 — the check costs 0.5 µs per shape |
| `PS2_GPU_HW_INDEP_PIXELS` | worst frame 112 ms |
| `PS2_GPU_SUBMIT_THREAD=1` | 78 bad frames |
| `PS2_FRAME_LATENCY=1` | 16 bad frames |
| `PS2_TILE=4` | 27% slower here (it wins in other games) |

Two of those tests were **invalid at first** because `PS2_GPU_HW=1`, the master switch, was
missing. The numbers above are the corrected ones.

---

## Built on

[ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp) by Samir Nuri, MIT
licence — the recompiler and the runtime that stands in for the console. `build.sh` clones it
at commit `5d1ebbc` into `.local/upstream` and applies the patches in `patches/`. Nothing of
the upstream is copied into this repository; that is deliberate, so it stays clear which work
is ours.

Also owed: [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp), the project the
runtime came from, and PCSX2, Play! and ps2sdk, whose behaviour was the reference for the
hardware and the system calls.

## Legal

Fate/unlimited codes and PlayStation are trademarks of their owners (TYPE-MOON, Cavia, Capcom,
Sony Interactive Entertainment). This project is unofficial and not affiliated with or endorsed
by any of them. The generated C++ and the built binary are translations of the game's
executable, so they belong to the game's owners: keep them on your own machine and do not
publish them. Use a disc image of a game you own.

The code in this repository is MIT, see [LICENSE](LICENSE).
