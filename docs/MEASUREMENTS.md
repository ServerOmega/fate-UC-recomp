# Measurements

Every number in the README comes from here. Machine: AMD Ryzen 5 5500U (6 cores, 12 logical
processors), 18 GB RAM, integrated AMD Radeon, Windows 10, Direct3D 12 unless stated.

The instrument is `PS2_FRAME_LOG`, which writes one line per frame:

```
frame wall_ms cpu_ms wait_window_ms wait_sound_ms shapes interpreted audio_ms
```

## The bottleneck

```
frame 3642:  total 646.96 ms
             waiting for the window  636.84 ms  (98.4%)
             emulation CPU            46.88 ms
             waiting for audio         0.01 ms
             shapes drawn             42,130
```

Distribution of bad frames by shape count, over 19,499 frames:

| Shapes in the frame | Bad frames (>21.28 ms) |
|---|---|
| more than 20,000 | 66 |
| 5,000 to 20,000 | 3 |
| 500 to 5,000 | 2 |
| fewer than 500 | 10 |

**81% of the bad frames have more than 20,000 shapes.** The rest are synchronisation: the
worst frame of one run took 66.93 ms with only 550 shapes on screen.

## Configurations, same metric

| Configuration | Frames | Average | Worst | >21 ms | >25 ms |
|---|---|---|---|---|---|
| Direct3D 12, stock | 5,999 | 16.71 | 64.50 | 14 | 10 |
| + low power | 5,999 | 16.68 | 33.91 | 4 | 3 |
| + QOS + immediate | 3,200 | 16.67 | 17.45 | 0 | 0 |

The last row is the menu (a light scene). In a real fight the same configuration measured
59.6 Hz average with a minimum of 45.8.

## In a real fight

| Session | Average Hz | % of 60 | Stretched audio |
|---|---|---|---|
| mailbox, sharpening on | 46.37 | 77.3% | 60.6% |
| **immediate, sharpening off** | **57.69** | **96.2%** | **7.8%** |

CPU use read from the window title: **204% with the sharpening filter, 102% without.**

## The audio stretcher is a thermometer

The ratio between sound produced and wall-clock time tracks Hz/60 exactly:

| Session | Real Hz | % of 60 | Sound/wall | Error | Stretched |
|---|---|---|---|---|---|
| 1st | 59.66 | 99.4% | 0.9943 | 0.004% | 4.0% |
| 2nd (`kEnter=0.99`) | 56.40 | 94.0% | 0.9400 | 0.010% | **32.1%** |
| 3rd | 57.64 | 96.1% | 0.9604 | 0.018% | 15.0% |
| 4th (low power) | 59.01 | 98.4% | 0.9911 | 0.003% | 6.3% |

Trying to fix the stretcher directly (lowering `kEnter` to 0.99) made it worse, not better:
with a looser threshold it enters later and stays in longer. It was reverted.

## Discarded paths

| Path | Result | Cause |
|---|---|---|
| Vulkan | worst frame 233.73 ms | AMD's Vulkan driver |
| OpenGL | does not start | wgpu #5299, "Could not lock adapter context" |
| CPU rasterizer | average 37.60 ms, 20.9 Hz | 31,818 shapes drawn in software |
| `PS2_GPU_HW_INDEP_MAX=all` | 132 frames >21 ms in 25,766 | the independence check costs 0.5 µs per shape |
| `PS2_GPU_HW_INDEP_PIXELS=65536` | worst frame 112.24 ms | synchronisation outweighs the gain |
| `PS2_GPU_SUBMIT_THREAD=1` | 78 bad frames | 1–2 ms of CPU per frame |
| `PS2_FRAME_LATENCY=1` | 16 bad frames | less queue, worse smoothness |
| `PS2_TILE=4` | average 21.66 ms | it wins in other games (38.9 → 42.0 fps there), not here |
| `PS2_TILE=16` | average 16.88 ms, 17 bad frames | no gain over the default 8 |

## Two traps that produced wrong numbers

**`--load-state` disables the frame log.** The loaded path runs single-threaded and
`PS2_FRAME_LOG` keeps only its header. Every measurement must use `--window` without
`--load-state`.

**`cpu_ms` is quantised to 15.625 ms.** Every observed value is an exact multiple (0, 15.62,
31.25, 46.88, 62.50, 78.12) — the resolution of Windows' `GetThreadTimes`. "cpu 46.88" means
"between 31 and 47 ms".

## Reproducing it

```bash
PS2_FRAME_LOG=C:/logs/frames.csv \
  ./.local/upstream/build/native-fate/ps2run.exe --disc .local/upstream/games/fate/disc.iso \
  --memcard .local/upstream/games/fate/card.ps2 \
  --game-dir .local/upstream/games/fate --window

python scripts/frame_report.py C:/logs/frames.csv
```

Three things decide whether a run is comparable: the same scene, no other process on the GPU,
and at least 19,000 frames so the boot sequence is behind you.
