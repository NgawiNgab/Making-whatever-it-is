#pragma once

#include <camfacesdk/Module.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace camface::features {

// The first real feature: an on-screen readout that measures the game's yaw
// convention on-device (T-CAM2 Phase A).
//
// It is deliberately an *observer* first. It draws nothing derived from an
// assumed convention and writes no game state; it reports what it sees, so the
// four cardinal readings that pin down yaw/pitch units and sign come from data.
// Once those readings exist, the snap logic in this same module turns on behind
// the same fact gate.
class CameraFacingModule final : public camfacesdk::Module {
public:
    // One definition of the id, shared by the menu registration and the draw calls.
    // A mismatch between those two would silently produce a module that toggles but
    // never draws, so they must not be able to drift.
    static constexpr std::string_view kModuleId = "cameraFacing";

    CameraFacingModule();

    [[nodiscard]] std::span<camfacesdk::FactRequirement const> requirements() const override;

    void onEnable() override;
    void onDisable() override;
    void onFrame(float deltaSeconds) override;
    void onConfigChanged(std::string_view key, std::string_view value) override;

    // Called by the runtime after facts are resolved, before first frame.
    void bind(bool hookInstalled, char const* hookStatus, bool rotationReady,
              char const* rotationReason, std::uint64_t* turnCallCounter);

    // The launcher-facing draw list. Filled each frame in onFrame().
    [[nodiscard]] std::vector<std::string> const& drawLines() const noexcept {
        return m_lines;
    }

    // Exposed for the log line the runtime emits once per second.
    [[nodiscard]] std::string statusText() const;

private:
    void rebuildLines();

    bool m_hookInstalled = false;
    std::string m_hookStatus = "not attempted";
    bool m_rotationReady = false;
    std::string m_rotationReason = "unknown";
    std::uint64_t* m_turnCalls = nullptr;
    std::uint64_t m_lastSeen = 0;

    std::atomic_bool m_captureNext{false};
    std::array<std::string, 8> m_captures{};
    std::size_t m_captureCount = 0;

    std::vector<std::string> m_lines;
    float m_elapsed = 0.0f;
};

}  // namespace camface::features
