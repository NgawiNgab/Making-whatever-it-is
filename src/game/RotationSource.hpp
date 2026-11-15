#pragma once

#include <camfacesdk/BuildData.hpp>

#include <cstdint>
#include <optional>

namespace camface::game {

// One decoded orientation. `yaw`/`pitch` are in the game's own units — which is
// precisely what T-CAM2 has not yet established, so nothing here assumes degrees.
struct Orientation {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// Which component the pair came from. Recorded so the log proves what was read
// rather than implying a semantic that has not been verified.
enum class SourceKind {
    None,
    TurnComponent,  // the EnTT component the turn path reads/writes
};

struct Reading {
    Orientation value;
    SourceKind kind = SourceKind::None;
    bool valid = false;
};

// Reads the live orientation. Implemented only when the build data carries a
// *verified* accessor; with the current data file (empty offsets table) this is
// inert and reports why. That is intentional: an unverified offset read is worse
// than no reading at all (docs/CROSS_VERSION.md section 5).
class RotationSource {
public:
    void configure(camfacesdk::BuildData const& build);

    // Called by the turn hook, which is the one place where a pointer to the
    // relevant component is available as a live argument.
    void observeTurnCall(void* self, void* deltaPtr);

    [[nodiscard]] Reading read() const;
    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] char const* unavailableReason() const noexcept { return m_reason; }

    // Number of turn calls observed. Proves the hook is live independent of
    // whether orientation reading is wired, which is what T-CAM2 needs.
    [[nodiscard]] std::uint64_t turnCalls() const noexcept { return m_turnCalls; }

private:
    bool m_ready = false;
    char const* m_reason = "no verified accessor in build data";
    std::uint64_t m_turnCalls = 0;
    void* m_self = nullptr;
    void* m_delta = nullptr;
};

}  // namespace camface::game
