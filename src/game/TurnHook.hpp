#pragma once

#include <camfacesdk/Facts.hpp>

#include <cstdint>

namespace camface::game {

class RotationSource;

// Installs the single game hook the V1 camera feature needs: the local turn path.
//
// Safety rules this class enforces (docs/SPEC.md section 5):
//  - HS-1: refuses to hook a target whose resolved length is below the engine's
//    patch size. The length comes from the verified data file, not a guess.
//  - HS-3: after installing, verifies the target still begins with the bytes the
//    signature matched, and logs the patched extent.
//  - HS-4: a hook that cannot be verified is removed again, never left half-on.
class TurnHook {
public:
    enum class Result {
        Installed,
        AlreadyInstalled,
        FactUnavailable,
        TargetTooSmall,
        EngineRefused,
        VerifyFailed,
    };

    // `facts` must outlive the hook. `observer` may be null (hook still installs).
    Result install(camfacesdk::Fact const& turnDeltaFact, RotationSource* observer,
                   std::uintptr_t runtimeAddress);

    void remove();
    [[nodiscard]] bool installed() const noexcept { return m_installed; }
    [[nodiscard]] static char const* describe(Result result);

    // Whether the detour should let the original run normally. When the feature is
    // engaged the detour suppresses input-driven turning (V1 hold).
    static void setEngaged(bool engaged);
    [[nodiscard]] static bool engaged();

private:
    bool m_installed = false;
};

}  // namespace camface::game
