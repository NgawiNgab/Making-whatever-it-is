#pragma once

#include <cstdint>
#include <optional>

namespace camface::game {

// One decoded orientation. Degrees are NOT assumed: the readout prints the raw
// floats, and the four-cardinal measurement is what tells us whether these are
// degrees, radians or something else.
struct Orientation {
    float pitch = 0.0f;  // component +0
    float yaw = 0.0f;    // component +4
    bool valid = false;
};

// Reads the local player's rotation the way BedrockTools does.
//
// The chain, and where each link comes from:
//   ClientInstance*            <- captured from the game's own update call
//                                 (hook on ClientInstanceUpdate, arg 0)
//   Player* localPlayer()      <- signature ClientInstanceGetLocalPlayer
//   Player* + 0x238            <- Actor::mActorRotationComponent, a *pointer* field
//   (*ptr) + 0                pitch
//   (*ptr) + 4                yaw
//
// Offsets and the deref chain are copied from BedrockTools
// (include/bedrocktools/sdk/offsets/World.hpp, sdk/world/Actor.hpp). They are
// INFERRED for 1.26.62, not measured here -- BedrockTools targets the same
// version family, which is evidence, not proof. The readout marks itself
// "UNVERIFIED" until a reading is sanity-checked against a known facing.
class RotationSource {
public:
    void setClientInstance(void* clientInstance);
    void setLocalPlayerGetter(std::uintptr_t getLocalPlayerRva);
    void setBaseAddress(std::uintptr_t base) { m_base = base; }

    // Fetches the player pointer. Called from the tick hook, after the original, so
    // the rotation component is already updated for this frame.
    void refreshLocalPlayer();

    [[nodiscard]] Orientation read() const;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] char const* state() const noexcept { return m_state; }

    [[nodiscard]] std::uint64_t turnCalls() const noexcept { return m_turnCalls; }
    void noteTurnCall() { ++m_turnCalls; }
    void noteTick() { ++m_ticks; }
    [[nodiscard]] std::uint64_t ticks() const noexcept { return m_ticks; }

private:
    void* m_clientInstance = nullptr;
    void* m_localPlayer = nullptr;
    std::uintptr_t m_getLocalPlayer = 0;  // runtime address, 0 = unresolved
    std::uintptr_t m_base = 0;
    char const* m_state = "no client instance yet";
    std::uint64_t m_turnCalls = 0;
    std::uint64_t m_ticks = 0;
};

}  // namespace camface::game
