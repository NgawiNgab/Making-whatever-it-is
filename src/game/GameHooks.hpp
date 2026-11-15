#pragma once

#include <cstddef>
#include <cstdint>

namespace camface::game {

class RotationSource;

// The two hooks that make a rotation read possible. Both follow BedrockTools
// (src/core/GameHooks.cpp): capture the ClientInstance from the game's own update
// call, then call the local-player getter on it.
//
// Every hook here is gated on its fact being AVAILABLE *and* its measured length
// exceeding the patch width. A hook that cannot be verified is not installed, and
// the readout says which fact failed instead of pretending to have data.
class GameHooks {
public:
    enum class Result {
        Installed,
        FactUnavailable,
        TargetTooSmall,
        EngineRefused,
        VerifyFailed,
    };

    struct Targets {
        // Runtime addresses. 0 means the fact did not resolve.
        std::uintptr_t clientInstanceUpdate = 0;
        std::uintptr_t normalTick = 0;
        std::uintptr_t getLocalPlayer = 0;
        std::size_t clientInstanceUpdateSize = 0;
        std::size_t normalTickSize = 0;
    };

    static Result install(Targets const& targets, RotationSource* rotation);
    static void remove();

    // True once the game has handed us a ClientInstance.
    [[nodiscard]] static bool clientCaptured();
    // True once any actor tick has been observed (proves NormalTick is live).
    [[nodiscard]] static bool tickObserved();
};

}  // namespace camface::game
