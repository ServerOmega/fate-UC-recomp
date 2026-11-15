# Fate/unlimited codes — native PC runner

A ready-to-run build. **No disc image is included**: you bring your own copy of the game, and
any filename works.

```
play.bat          the launcher - double-click it
ps2run.exe        the game, translated from the PS2 executable
SDL2.dll          window and input
dxcompiler.dll    shader compiler (Direct3D 12)
dxil.dll          shader compiler support
keys.ini          the controls, in plain text
```

---

## Run it

1. Put your Fate/unlimited codes disc image in this folder. **Any name, any case** — `.iso` or
   `.bin`, spaces and parentheses are fine.
2. Double-click **`play.bat`**.

It finds the image by itself. If there is more than one, pass the one you want:

```
play.bat "D:\games\my other copy.iso"
```

You can also **drag the image onto `play.bat`**.

A memory card (`card.ps2`) is created on the first run. Saving from the game writes to it.

---

## Controls

| Key | Button |
|---|---|
| **Arrows** | move — they drive the d-pad **and** the left stick, because the menus read the stick and the fighting game reads the d-pad |
| **A S D F** | square, triangle, circle, cross |
| **Z X C V** | L1, L2, R1, R2 |
| **Enter** | start |
| **Tab** | select |
| **Space** | cross (cancel) |
| **F2** | quick save |
| **F4** | quick load |
| **F1** | quick menu (save, load, picture, resolution, keys, quit) |
| **F11** | full screen |

This is a Japanese game: **circle (D) confirms, cross (F or Space) goes back.**

To change a key, edit `keys.ini` (plain text, one key per action), or use the **F1** menu.

---

## What is in the build

The game's own code is **100% native**: the Emotion Engine (162 translation units), both vector
units (**405 compiled programs**) and the I/O processor were translated to C++ and compiled.
The console is imitated by the runtime.

**405 vector programs** matters: an early build of this release had only 3, and the runner
compiled the rest while the game ran, which stutters every time a new move or effect appears.
The 405 here cover the game as it has been played so far.

### Settings, already applied

These are compiled into `ps2run.exe`; `play.bat` also sets them so you can see them:

| Setting | Effect |
|---|---|
| `PS2_GPU_LOW_POWER=1` | stops the GPU climbing its clock and then throttling (worst frame 64.5 → 33.9 ms) |
| `PS2_QOS=1` | keeps Windows from parking the emulation threads on the small cores |
| `PS2_PRESENT_MODE=immediate` | no vsync wait: 46.4 → 57.7 pictures a second |
| `PS2_SHARP_2D=0` | no 2D sharpening filter: **half the CPU**, 204% → 102% |
| `PS2_SCALE=1` | the console's own resolution |

Measured in a real fight on a Ryzen 5 5500U with integrated Radeon: **57.69 Hz, 96.2% of the
console's speed, 7.8% of the sound stretched.** Before these settings: 46.37 Hz and 60.6%
stretched.

Every one can be overridden by setting the variable before running, and `PS2_TUNED=0` gives
back the upstream defaults exactly.

---

## If something is wrong

**The picture tears in fast movement.** That is `PS2_PRESENT_MODE=immediate`. Edit `play.bat`
and change it to `mailbox`: smoother, but it measured 46.4 Hz instead of 57.7.

**Text and icons look soft.** Also the sharpening setting. Set `PS2_SHARP_2D=1` in `play.bat`
and accept half the CPU back.

**It stutters when a new effect appears.** The vector program for that effect was not in the
build and is being compiled while the game runs (a message says so in the log). It settles after
the first time.

**"no disc image found".** The image is not in this folder, or its extension is not `.iso` or
`.bin`. Drag it onto `play.bat` instead.

**"ps2run.exe is not next to this file".** The four program files were separated. Keep them in
one folder.

**It does not open at all.** Run `play.bat` from a command prompt to see the log. The last lines
usually name the cause.

---

## What is still slow, and why

A special move sends about **42,000 shapes in a single frame**. Of the slow frames, **81% have
more than 20,000 shapes**: it is the effect, not noise. The time is lost presenting those shapes,
not in emulation — the runner's own cost is 15 to 46 ms while the window takes up to 637 ms.

That is the graphics chip being imitated with exactness on an integrated GPU. The game remains
playable at 46 to 60 pictures a second, but a heavy special will still cost a few frames.

---

## Legal

Fate/unlimited codes and PlayStation are trademarks of their owners. This build is unofficial
and not affiliated with or endorsed by them. `ps2run.exe` is a translation of the game's own
executable, so it belongs to the game's owners: keep it to yourself and do not redistribute it.
Use a disc image of a game you own.

The runtime underneath is [ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp)
by Samir Nuri, MIT licence. This build's own work — the native IOP, the tuned defaults and the
build fixes — is at
[fate-UC-recomp](https://github.com/ohackercego/fate-UC-recomp).
