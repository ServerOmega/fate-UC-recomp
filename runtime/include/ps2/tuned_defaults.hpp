// Defaults for the run that were tuned on one machine and measured, so that the
// plain ps2run.exe (and a double-click on play-fate.bat) comes up with them instead
// of the upstream defaults. Every one can still be overridden by the environment
// variable named beside it: PS2_TUNED=0 turns all of them off and gives back the
// upstream behaviour exactly.
//
// Why each one, all measured on a Ryzen 5 5500U with its integrated Radeon, Fate
// unlimited codes, Direct3D 12 (see README.md, "The settings that work"):
//
//   LowPower    the high-performance request makes the integrated GPU climb its
//               clock and then throttle, which shows up as spikes of 60 ms in a
//               frame that should take 16.7. Asking for low power instead keeps a
//               steady clock: the worst frame went from 64.5 to 33.9 ms.
//   QOS         Windows' EcoQoS parks the emulation threads on the small cores of a
//               heterogenous CPU. Opting out steadies the frame time.
//   Immediate   Mailbox holds one more picture in flight, so a heavy frame costs
//               three. Presenting immediately measured 46.4 -> 57.7 pictures a
//               second; the cost is possible tearing in fast movement.
//   Sharp2D 0   the sharpening pass for upscaled 2D art (text, icons, menus) costs
//               half of the CPU: 204% -> 102%, and the stretched sound fell from
//               60.6% to 7.8% of the run with it.
#pragma once

#include <cstdlib>
#include <cstring>

namespace ps2::tuned {

// The environment variable wins whenever it is set; these are only what a run
// starts with when it is not.
inline bool enabled() {
    const char *off = std::getenv("PS2_TUNED");
    return !(off && std::strcmp(off, "0") == 0);
}

inline bool low_power() {
    if (std::getenv("PS2_GPU_LOW_POWER")) return true;
    return enabled();
}

inline bool qos() {
    if (const char *v = std::getenv("PS2_QOS")) return std::strcmp(v, "0") != 0;
    return enabled();
}

// "mailbox", "fifo", "relaxed", "immediate", or null for the surface's own choice.
inline const char *present_mode() {
    if (const char *v = std::getenv("PS2_PRESENT_MODE")) return v;
    return enabled() ? "immediate" : nullptr;
}

// -1 when the variable was not set and the tuning is off; otherwise 0..2.
inline int sharp_2d() {
    if (const char *v = std::getenv("PS2_SHARP_2D")) return std::atoi(v);
    return enabled() ? 0 : -1;
}

}  // namespace ps2::tuned
