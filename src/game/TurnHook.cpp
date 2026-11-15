#include "TurnHook.hpp"

#include "RotationSource.hpp"

#include <pl/Logger.hpp>
#include <pl/memory/Hook.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace camface::game {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

// The reference's calling convention, corroborated on 1.26.62 by the confirmed
// disassembly of the turn function: x0 = self, x1 = pointer to the delta pair
// (evidence F-CAM-04: `mov x19, x0` / `mov x20, x1` / `ldp s0, s1, [x20]`).
using TurnDeltaFn = void (*)(void* self, float* deltaXY);

TurnDeltaFn g_original = nullptr;
RotationSource* g_observer = nullptr;
std::atomic_bool g_engaged{false};

// The target we patched, remembered so unhook uses the same key the engine did.
void* g_target = nullptr;

// Gloss writes an inline patch of at least this many bytes. Named because HS-1 is a
// *measured* rule derived from a real crash (evidence F-026), not a preference.
constexpr std::size_t kMinPatchWidth = 12;

void turnDeltaDetour(void* self, float* deltaXY) {
    if (g_observer != nullptr) g_observer->observeTurnCall(self, deltaXY);

    if (g_engaged.load(std::memory_order_acquire)) {
        // V1 hold: stop input-driven turning while the feature owns the view.
        // Zeroing the delta (rather than skipping the original) keeps the game's own
        // bookkeeping inside the original intact.
        float suppressed[2] = {0.0f, 0.0f};
        if (g_original != nullptr) g_original(self, suppressed);
        return;
    }

    if (g_original != nullptr) g_original(self, deltaXY);
}

}  // namespace

char const* TurnHook::describe(Result result) {
    switch (result) {
        case Result::Installed: return "installed";
        case Result::AlreadyInstalled: return "already installed";
        case Result::FactUnavailable: return "fact unavailable";
        case Result::TargetTooSmall: return "target shorter than patch width";
        case Result::EngineRefused: return "engine refused the hook";
        case Result::VerifyFailed: return "post-install verification failed";
    }
    return "unknown";
}

TurnHook::Result TurnHook::install(camfacesdk::Fact const& turnDeltaFact, RotationSource* observer,
                                   std::uintptr_t runtimeAddress) {
    if (m_installed) return Result::AlreadyInstalled;
    if (!turnDeltaFact.available() || runtimeAddress == 0) return Result::FactUnavailable;

    // HS-1, enforced from measured data. `minSize` is the function length recorded
    // when the pattern was verified. No measurement means no permission: a missing
    // number must not silently become "assume it is big enough".
    if (turnDeltaFact.minSize == 0) {
        log().warn("turn hook refused: no measured length for {} (rva 0x{:x})", turnDeltaFact.id,
                   turnDeltaFact.rva);
        return Result::TargetTooSmall;
    }
    if (turnDeltaFact.minSize <= kMinPatchWidth) {
        log().warn("turn hook refused: {} measures {} bytes, patch needs > {}", turnDeltaFact.id,
                   turnDeltaFact.minSize, kMinPatchWidth);
        return Result::TargetTooSmall;
    }

    // HS-3: verify the site *before* patching. A detour overwrites the first
    // instruction, so checking these bytes afterwards would prove nothing; checking
    // them first proves the address still holds the function that was verified.
    const auto* site = reinterpret_cast<const std::uint8_t*>(runtimeAddress);
    if (turnDeltaFact.expectPrefixLength != 0) {
        const std::size_t compare =
            turnDeltaFact.expectPrefixLength < turnDeltaFact.expectPrefix.size()
                ? turnDeltaFact.expectPrefixLength
                : turnDeltaFact.expectPrefix.size();
        if (std::memcmp(site, turnDeltaFact.expectPrefix.data(), compare) != 0) {
            log().error("turn hook refused: site 0x{:x} no longer matches the verified prefix",
                        runtimeAddress);
            return Result::VerifyFailed;
        }
    }

    g_observer = observer;
    void* target = reinterpret_cast<void*>(runtimeAddress);
    if (pl::memory::hook(target, reinterpret_cast<void*>(&turnDeltaDetour),
                         reinterpret_cast<void**>(&g_original)) != 0) {
        g_observer = nullptr;
        g_original = nullptr;
        return Result::EngineRefused;
    }

    g_target = target;
    m_installed = true;
    log().info("turn hook installed at 0x{:x} (rva 0x{:x}, measured length {} bytes)",
               runtimeAddress, turnDeltaFact.rva, turnDeltaFact.minSize);
    return Result::Installed;
}

void TurnHook::remove() {
    if (!m_installed) return;

    // Order matters: stop forwarding to the observer first, then drop the detour.
    g_observer = nullptr;
    g_engaged.store(false, std::memory_order_release);
    if (g_target != nullptr) {
        pl::memory::unhook(g_target, reinterpret_cast<void*>(&turnDeltaDetour));
    }
    g_target = nullptr;
    g_original = nullptr;
    m_installed = false;
}

void TurnHook::setEngaged(bool engaged) { g_engaged.store(engaged, std::memory_order_release); }

bool TurnHook::engaged() { return g_engaged.load(std::memory_order_acquire); }

}  // namespace camface::game
