#include "FramePump.hpp"

#include <pl/Logger.hpp>
#include <pl/memory/Hook.hpp>

#include <atomic>
#include <dlfcn.h>
#include <EGL/egl.h>

namespace camface::platform {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);

SwapBuffersFn g_original = nullptr;
FramePump::Callback g_callback = nullptr;
std::atomic<std::uint64_t> g_frames{0};
std::atomic_bool g_running{false};

// Naive but adequate monotonic clock: the callback only needs a plausible delta to
// throttle itself, and a monotonic source never goes backwards across suspend.
std::int64_t nowNanos() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

std::int64_t g_lastNanos = 0;

EGLBoolean swapBuffersDetour(EGLDisplay display, EGLSurface surface) {
    const EGLBoolean result = g_original != nullptr ? g_original(display, surface) : EGL_FALSE;

    // Act only when a real context was presented; the launcher and other surfaces
    // also swap, and ticking for those would be noise.
    if (g_callback != nullptr && eglGetCurrentContext() != EGL_NO_CONTEXT) {
        const std::int64_t now = nowNanos();
        float delta = 0.0f;
        if (g_lastNanos != 0) {
            delta = static_cast<float>(now - g_lastNanos) / 1.0e9f;
            // Clamp: a long stall (pause, app switch) must not look like one huge frame.
            if (delta > 0.25f) delta = 0.25f;
        }
        g_lastNanos = now;
        g_frames.fetch_add(1, std::memory_order_relaxed);
        g_callback(delta);
    }
    return result;
}

}  // namespace

bool FramePump::start(Callback callback) {
    if (g_running.load(std::memory_order_acquire)) return true;

    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    if (egl == nullptr) {
        log().warn("frame pump unavailable: libEGL.so not loaded");
        return false;
    }
    void* symbol = dlsym(egl, "eglSwapBuffers");
    if (symbol == nullptr) {
        log().warn("frame pump unavailable: eglSwapBuffers not found");
        dlclose(egl);
        return false;
    }

    g_callback = callback;
    g_lastNanos = 0;
    if (pl::memory::hook(symbol, reinterpret_cast<void*>(&swapBuffersDetour),
                         reinterpret_cast<void**>(&g_original)) != 0) {
        g_callback = nullptr;
        dlclose(egl);
        log().warn("frame pump unavailable: hook refused");
        return false;
    }

    g_running.store(true, std::memory_order_release);
    log().info("frame pump started (eglSwapBuffers)");
    return true;
}

void FramePump::stop() {
    if (!g_running.exchange(false, std::memory_order_acq_rel)) return;

    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    if (egl != nullptr) {
        if (void* symbol = dlsym(egl, "eglSwapBuffers")) {
            pl::memory::unhook(symbol, reinterpret_cast<void*>(&swapBuffersDetour));
        }
        dlclose(egl);
    }
    g_callback = nullptr;
    g_original = nullptr;
}

bool FramePump::running() { return g_running.load(std::memory_order_acquire); }

std::uint64_t FramePump::frameCount() { return g_frames.load(std::memory_order_relaxed); }

}  // namespace camface::platform
