#include "CameraFacingModule.hpp"

#include <pl/Logger.hpp>
#include <pl/ModMenu.hpp>

#include <cstdio>

namespace camface::features {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

constexpr camfacesdk::FactRequirement kRequirements[] = {
    // Without these three there is no player pointer and therefore no yaw.
    {"ClientInstanceUpdate", true},
    {"NormalTick", true},
    {"ClientInstanceGetLocalPlayer", true},
};

}  // namespace

CameraFacingModule::CameraFacingModule() : Module(std::string(kModuleId), "Camera Facing") {}

std::span<camfacesdk::FactRequirement const> CameraFacingModule::requirements() const {
    return kRequirements;
}

void CameraFacingModule::bind(game::RotationSource* rotation, char const* hookStatus,
                              bool hooksInstalled) {
    m_rotation = rotation;
    m_hookStatus = hookStatus != nullptr ? hookStatus : "unknown";
    m_hooksInstalled = hooksInstalled;
}

void CameraFacingModule::onEnable() {
    m_captureNext = false;
    log().info("readout enabled");
}

void CameraFacingModule::onDisable() {
    m_lines.clear();
    // Clear the overlay so a disabled module leaves nothing behind
    // (armorhud.cpp:548-552 does the same).
    pl::modmenu::submitDrawCommands(kModuleId, {});
    pl::modmenu::submitHudEditorElements(kModuleId, {});
}

void CameraFacingModule::onConfigChanged(std::string_view key, std::string_view value) {
    if (key == "mark") {
        (void)value;
        m_captureNext = true;
    }
}

void CameraFacingModule::rebuildLines() {
    m_lines.clear();
    char buffer[192];

    m_lines.emplace_back("Camera Facing");

    if (!m_hooksInstalled) {
        m_lines.emplace_back("hooks: " + m_hookStatus);
        return;
    }

    if (m_rotation == nullptr) {
        m_lines.emplace_back("no rotation source");
        return;
    }

    const game::Orientation orientation = m_rotation->read();
    if (!orientation.valid) {
        // Say why, rather than printing 0.0 and implying the player faces north.
        m_lines.emplace_back(std::string("yaw: -- (") + m_rotation->state() + ")");
    } else {
        std::snprintf(buffer, sizeof(buffer), "yaw %.3f  pitch %.3f", orientation.yaw,
                      orientation.pitch);
        m_lines.emplace_back(buffer);
    }

    std::snprintf(buffer, sizeof(buffer), "tick %llu  turn %llu",
                  static_cast<unsigned long long>(m_rotation->ticks()),
                  static_cast<unsigned long long>(m_rotation->turnCalls()));
    m_lines.emplace_back(buffer);

    if (m_markCount != 0) m_lines.emplace_back("last: " + m_marks[m_markCount - 1]);
}

void CameraFacingModule::onFrame(float deltaSeconds) {
    m_elapsed += deltaSeconds;

    if (m_captureNext) {
        m_captureNext = false;
        if (m_rotation != nullptr) {
            const game::Orientation orientation = m_rotation->read();
            char buffer[128];
            if (orientation.valid) {
                std::snprintf(buffer, sizeof(buffer), "yaw %.3f pitch %.3f", orientation.yaw,
                              orientation.pitch);
            } else {
                std::snprintf(buffer, sizeof(buffer), "-- (%s)", m_rotation->state());
            }
            if (m_markCount < m_marks.size()) {
                m_marks[m_markCount++] = buffer;
            }
            // The log line is the artifact: the whole point of marking is a labelled
            // value in logcat that can be compared against a known facing.
            log().info("MARK {}: {}", static_cast<int>(m_markCount), buffer);
        }
    }

    if (m_elapsed < 0.1f) return;
    m_elapsed = 0.0f;
    rebuildLines();

    // Draw. Pattern copied from BedrockTools' HUD modules: rebuild the command list
    // every frame and submit it, alongside the editor element that makes the block
    // draggable in the HUD editor.
    if (m_lines.empty()) return;

    std::vector<pl::modmenu::DrawCommand> commands;
    commands.reserve(m_lines.size() + 1);

    const float lineHeight = 18.0f;
    float y = hudPosY;

    pl::modmenu::DrawCommand background;
    background.type = pl::modmenu::DrawCommandType::RectFilled;
    background.x = hudPosX - 4.0f;
    background.y = hudPosY - 4.0f;
    background.w = 220.0f;
    background.h = lineHeight * static_cast<float>(m_lines.size()) + 8.0f;
    background.color = 0x99000000u;  // ARGB: translucent black for legibility
    commands.push_back(background);

    for (auto const& line : m_lines) {
        pl::modmenu::DrawCommand command;
        command.type = pl::modmenu::DrawCommandType::Text;
        command.x = hudPosX;
        command.y = y;
        command.w = 220.0f;
        command.h = lineHeight;
        command.size = 14.0f;
        command.color = 0xFFFFFFFFu;
        command.text = line;
        commands.push_back(command);
        y += lineHeight;
    }

    pl::modmenu::submitDrawCommands(kModuleId, commands);

    pl::modmenu::HudEditorElement element;
    element.elementId = "camface.readout";
    element.displayName = "Camera Facing readout";
    element.positionKeyX = "hudPosX";
    element.positionKeyY = "hudPosY";
    element.snapGroup = "camface";
    element.x = hudPosX;
    element.y = hudPosY;
    element.width = 220.0f;
    element.height = lineHeight * static_cast<float>(m_lines.size()) + 8.0f;
    element.gridSize = 4.0f;
    element.snapThreshold = 12.0f;
    element.gridGap = 4.0f;
    element.snapFlags = pl::modmenu::HudSnapGrid | pl::modmenu::HudSnapScreenCenter;
    pl::modmenu::submitHudEditorElements(kModuleId, std::span<const pl::modmenu::HudEditorElement>(&element, 1));
}

std::string CameraFacingModule::statusText() const {
    if (m_rotation == nullptr) return "no rotation source";
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "hooks=%s ticks=%llu turn=%llu state=%s",
                  m_hookStatus.c_str(), static_cast<unsigned long long>(m_rotation->ticks()),
                  static_cast<unsigned long long>(m_rotation->turnCalls()), m_rotation->state());
    return buffer;
}

std::string CameraFacingModule::lastMark() const {
    return m_markCount == 0 ? std::string("none") : m_marks[m_markCount - 1];
}

}  // namespace camface::features
