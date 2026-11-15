# The IOP modules

The PS2's IOP runs small programs called IRX modules. Fate/unlimited codes does not ship them
as files on the disc: they are packed inside `MODULES/IOPRP310.IMG`, which the game loads
through `LoadModuleBuffer` at boot.

The log says so on the first frames:

```
[sif] IOP reboot requested with "rom0:UDNL cdrom0:\MODULES\IOPRP310.IMG;1"
[sif] LOADFILE from cdrom0:\MODULES\IOPRP310.IMG;1 reports 3100
```

## What is in the image

`IOPRP310.IMG` is a sequence of IRX images. Each one has the IRX header (magic `PS-X EXE`),
the module's entry point, its text and data sizes, and its name in a `.rodata` string.

Extracting them is a matter of walking the image: read the header, take `text_size` bytes at
`text_addr` and `data_size` at `data_addr`, and write them out as one `.irx` per module. The
generator expects them in one directory, one file per module, and looks the module up by the
name the game passes to `LoadModuleBuffer`.

## Why they are not in the repository

An extracted IRX is game data: it is the game's own sound driver, disc streaming code and
controller driver, compiled by the game's publisher. Distributing them would be distributing
part of the game. `build.sh` therefore reads them from **your** disc and writes them to
`.local/`, which `.gitignore` excludes.

## What the generator does with them

`recomp/r3000_gen.cpp` and `recomp/gen_iop.cpp` walk each module's control flow, find the
functions, and emit C++ per function — one translation unit per module, plus a table that maps
each module entry to its compiled body.

`runtime/src/iop/r3000_native.cpp` is the bridge that runs it. Three details matter and are
commented in the code:

1. **Modules are matched by name, not by load order.** The order the game loads them in is not
   the order of any table.
2. **The cycle budget is charged per entry, by the entry's real cost.** Charging one cycle per
   instruction made the IOP run about 28 times too fast, which desynchronised the sound timer.
3. **A branch in a delay slot cancels the branch.** Upstream's interpreter gets this right;
   the generated code has to as well, and the bridge handles the return-with-nop case
   (`c.pc += 4`), which is what removed the last 840 escapes.

## The report

On Fate/unlimited codes the bridge reports:

```
20 modules, 100.00% dispatch, 0 escapes
```

**Dispatch** is the share of `MSCAL`-like module entries that found a compiled body.
**Escapes** are entries that fell back to interpreting. Both numbers are printed on every run
with `PS2_IOP_NATIVE_STATS=1`, so a regression shows up immediately instead of as a mystery
crash later.
