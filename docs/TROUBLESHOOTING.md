# Troubleshooting the build

The build was verified end to end on Windows 10 with this exact setup:

```
[3/6] machine code to C++   162 units written
[4/6] exploring             runtime: built 57 files
[5/6] vector unit programs  3 VU programs, 2 of them jump targets
[6/6] builds                Play it natively: build/native-fate/ps2run
```

If it does not get there, the cause is almost always one of the six below. Every one of them
was hit for real while getting it to work, and each is fixed in `build.sh` or in a patch.

## 1. `The source directory "/c/..." does not exist`

**The single most common failure on Windows, and it bit four different scripts.**

`git`, `cmake` and `clang` on Windows are **native programs**. They do not understand the
`/c/Fate/...` form that the shell (MSYS/git-bash) uses. Hand them a POSIX path and they look
for a directory called `c` at the root of the current drive, which is not what anyone meant.

The symptom is confusing because the script looks correct: the path is right for the shell, and
only the child process misreads it.

`build.sh` converts everything it passes outwards with `cygpath -m`, and the patches add the same
conversion to the upstream scripts that needed it:

| File | What it computes |
|---|---|
| `scripts/setup.sh` | `ROOT` — **this one calls cmake for the tools, and it was the last to fail** |
| `scripts/cxx.sh` | `ROOT`-independent, but its probe directory (`mktemp -d`) was in POSIX form |
| `scripts/port.sh` | `ROOT` |
| `build.sh` | `RAIZ`, `TRABALHO`, and any relative ISO argument |

If you add a script of your own, add the line:

```bash
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SOMETHING="$(cygpath -m "$SOMETHING")" ;; esac
```

## 2. `no clang C++ compiler with <format> found`

Two separate causes, and both were hit.

**a. The compiler is not on the PATH.** The clang that works here is LLVM-MinGW installed by
WinGet, which does not put itself on the PATH. `build.sh` searches for it explicitly:

```
C:/Users/<you>/AppData/Local/Microsoft/WinGet/Packages/
    MartinStorsjo.LLVM-MinGW.UCRT_*/llvm-mingw-*/bin/clang.exe
```

Install it with `winget install MartinStorsjo.LLVM-MinGW.UCRT`, or set `CXX` yourself to a
clang 17 or newer.

**b. The probe runs in a POSIX directory.** `scripts/cxx.sh` builds its test in `mktemp -d`,
which on this shell returns `/tmp/tmp.XXXX`. A native clang cannot read that, so the test
fails and a perfectly good compiler is discarded. The patch converts the probe path with
`cygpath -m`.

Note that the probe **requires `clang++`**, not `clang`: it compiles and links C++, and only the
`clang++` driver links the C++ standard library by default.

## 3. `CMakeLists.txt:8 needs clang++ ... is GNU`

The compiler was found but CMake did not receive it. `scripts/cxx.sh` turns the found compiler
into `-DCMAKE_CXX_COMPILER=<path>`, and that flag has to reach every `cmake -S` call. If you
call cmake yourself, pass it:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER="C:/path/to/clang++.exe"
```

There are three `cmake` on the PATH on this machine (Program Files, WinGet, msys64). The one
first on the PATH wins; version 4.4 was used here and works.

## 4. `CXX=... does not work here: it must be clang (GCC lacks the vector builtins)`

Misleading wording, from `scripts/cxx.sh` line 57. It prints this whenever the *probe* failed,
whatever the reason, and the reason is nearly always cause 1b above (a POSIX probe directory),
not a bad compiler. Fix the probe path and the message disappears.

The message above it is the useful one: `CXX=... is not a working clang ..., so ... builds
instead` means the probe succeeded for a different compiler and that one will be used.

## 5. `patch does not apply`

The patches are context diffs of the upstream at commit `5d1ebbc`. Three things to check:

1. **The right commit.** `build.sh` checks out `5d1ebbc` explicitly; a plain clone lands on the
   HEAD, where the context has moved.
2. **Line endings.** The clones are CRLF and the patches are LF. With `core.autocrlf=true` (the
   Git for Windows default) `git apply` normalises both sides and the patch applies. Forcing
   `core.autocrlf=false` in the clone **breaks it** — that was tried, and the error became
   `patch does not apply` on a file that had applied a minute earlier.
3. **A stale half-applied tree.** Delete `.local/upstream` and let `build.sh` clone it again.

## 6. `The source directory "/c/..." does not exist` in the *web* build only

`port.sh` also builds a WebAssembly version, which needs Emscripten and has the same POSIX-path
problem in a place the patches do not reach. It is not needed: `build.sh` passes
`--native-only`, which skips it. If you call `port.sh` yourself, add that flag.

The earlier run that stopped here had **already produced a working native `ps2run.exe`** — the
web build runs after it. So if you ever see this, check `build/native-fate/ps2run.exe` before
assuming the build failed.

## Verifying it worked

```bash
.local/upstream/build/native-fate/ps2run.exe \
    --disc .local/upstream/games/fate/disc.iso \
    --game-dir .local/upstream/games/fate --frames 120
```

A healthy run ends with:

```
[disc] games/fate/disc.iso: 35 files, 675392 sectors, layer 1 at 0
[gpu] graphics API: Direct3D 12 (the default on Windows; ...)
[ee] stopped after 120 frames, ... cycles: frame limit reached
```

`35 files, 675392 sectors` is Fate/unlimited codes (`SLPM-55108`). A different count means a
different disc.
