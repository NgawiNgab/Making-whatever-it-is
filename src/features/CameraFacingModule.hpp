#pragma once

#include <camfacesdk/Module.hpp>

#include "../game/RotationSource.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace camface::features {

// The on-screen readout. Its only job is to show the live yaw so the convention can
// be measured in four readings, and to log a labelled sample each time the user
// presses "Mark reading".
//
// Drawing follows BedrockTools' HUD modules exactly (src/modules/hud/armorhud.cpp,
// compass.cpp): submit draw commands AND an editor element every frame, with a
// position that the HUD editor can move.
class CameraFacingModule final : public camfacesdk::Module {
public:
    static constexpr std::string_view kModuleId = "cameraFacing";

    CameraFacingModule();

    [[nodiscard]] std::span<camfacesdk::FactRequirement const> requirements() const override;

    void onEnable() override;
    void onDisable() override;
    void onFrame(float deltaSeconds) override;
    void onConfigChanged(std::string_view key, std::string_view value) override;

    void bind(game::RotationSource* rotation, char const* hookStatus, bool hooksInstalled);

    // HUD position, persisted by the launcher's config, matching the reference's
    // hudPosX / hudPosY convention (compass.hpp:22).
    float hudPosX = 20.0f;
    float hudPosY = 120.0f;

    [[nodiscard]] std::vector<std::string> const& drawLines() const noexcept { return m_lines; }
    [[nodiscard]] std::string statusText() const;

    // Snapshot of the last marked reading, for the log.
    [[nodiscard]] std::string lastMark() const;

private:
    void rebuildLines();

    game::RotationSource* m_rotation = nullptr;
    std::string m_hookStatus = "not attempted";
    bool m_hooksInstalled = false;

    bool m_captureNext = false;
    std::array<std::string, 8> m_marks{};
    std::size_t m_markCount = 0;

    std::vector<std::string> m_lines;
    float m_elapsed = 0.0f;
};

}  // namespace camface::features
