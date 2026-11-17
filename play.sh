#!/usr/bin/env bash
# ==========================================================================
#  Fate/unlimited codes - native PC runner (Linux)
# ==========================================================================
#
#  Put your own disc image next to this file. Any name works: .iso, .bin, .img.
#  The launcher finds it, so you do not edit anything.
#
#  Usage:
#    * ./play.sh
#    * ./play.sh "/path/to/my copy of fate.iso"
#
#  No disc image is included. You bring a copy of a game you own.
#
#  The picture is Vulkan (wgpu). On KDE Plasma the window is Wayland when SDL
#  picks it, and X11 otherwise. SDL2 and libvulkan.so.1 come from the system.
# ==========================================================================

set -eu

cd "$(cd "$(dirname "$0")" && pwd)"

# ------------------------------------------------ tuning (measured, see README)
# PS2_QOS is applied on Windows only (EcoQoS). Set here so the launch matches
# play.bat; the Linux build ignores it.
export PS2_SCALE=1
export PS2_GPU_LOW_POWER=1
export PS2_QOS=1
export PS2_PRESENT_MODE=immediate
export PS2_SHARP_2D=0

RUNNER=./ps2run
if [ ! -f "$RUNNER" ]; then
    echo "ERROR: ps2run is not next to this file." >&2
    echo >&2
    echo "  The build puts ps2run, play.sh and keys.ini in one folder" >&2
    echo "  (.local/upstream/build/native-fate/ after ./build.sh)." >&2
    exit 1
fi
if [ ! -x "$RUNNER" ]; then
    echo "ERROR: ps2run is not executable: $PWD/ps2run" >&2
    exit 1
fi

# ------------------------------------------------ find the disc image
ISO="${1:-}"
if [ -n "$ISO" ]; then
    if [ ! -f "$ISO" ]; then
        echo "ERROR: the file you gave does not exist:" >&2
        echo "  $ISO" >&2
        exit 1
    fi
else
    # No argument: the first disc image in this folder. An ISO is what this was
    # built against; .bin is the raw dump; .img is the same kind of image.
    ISO=""
    for ext in iso ISO bin BIN img IMG; do
        for f in *."$ext"; do
            [ -f "$f" ] || continue
            ISO="$f"
            break 2
        done
    done
    if [ -z "$ISO" ]; then
        echo "ERROR: no disc image found in this folder." >&2
        echo >&2
        echo "  Put your Fate/unlimited codes image here (.iso, .bin or .img) and run" >&2
        echo "  this file again, or pass the image as an argument." >&2
        echo >&2
        echo "  Current folder: $PWD" >&2
        exit 1
    fi
fi

echo "Starting Fate/unlimited codes..."
echo "  disc: $ISO"
echo

# A memory card is created on first run if it is not there. It is the
# conventional 8 MB PS2 card layout, and saving from the game writes to it.
CARD=card.ps2

echo "  Controls:"
echo "    Arrows    move (d-pad and left stick together)"
echo "    A S D F   square / triangle / circle / cross"
echo "    Z X C V   L1 / L2 / R1 / R2"
echo "    Enter     start        Tab    select"
echo "    Space     cross (cancel)"
echo "    F2        quick save   F4     quick load"
echo "    F1        quick menu   F11    full screen"
echo
echo "  Japanese game: circle (D) confirms, cross (F or Space) goes back."
echo

set +e
"$RUNNER" --disc "$ISO" --memcard "$CARD" --window
CODE=$?
set -e

if [ "$CODE" -ne 0 ]; then
    echo
    echo "The runner stopped with code $CODE."
    echo "A log was written beside the disc image; the last lines usually say why."
    if [ -t 0 ]; then
        printf 'Press Enter to close.\n'
        read -r _
    fi
fi
exit "$CODE"
