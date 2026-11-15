#pragma once

#include <cstdint>

namespace camface::platform {

// Drives the mod's per-frame work.
//
// The preloader SDK deliberately exposes no frame callback, so the tick has to come
// from somewhere in the render loop. `eglSwapBuffers` is a *public* libEGL symbol:
// hooking it needs no game signature, no RVA, and survives a Minecraft update
// (verified against the reference, src/core/GameHooks.cpp:283-292).
//
// The swap happens on the render thread, so callbacks run there: they must not block.
class FramePump {
public:
    using Callback = void (*)(float deltaSeconds);

    // Hooks eglSwapBuffers and calls `callback` after each swap. Returns false if
    // libEGL or the symbol is unavailable, in which case frames simply do not tick
    // and the mod stays in its startup-only mode (the readout still exists, it just
    // never refreshes).
    static bool start(Callback callback);
    static void stop();

    [[nodiscard]] static bool running();
    [[nodiscard]] static std::uint64_t frameCount();
};

}  // namespace camface::platform
