#include "CameraFacingModule.hpp"

#include <camfacesdk/Pattern.hpp>

#include <cstdio>

namespace camface::features {
namespace {

// Facts this module needs before it may touch the game at all.
constexpr camfacesdk::FactRequirement kRequirements[] = {
    {"LocalPlayerApplyTurnDelta", true},
    // Nice-to-have: used for the local-player check, but not required to observe
    // the turn path, and still INFERRED (evidence F-CAM-08).
    {"ClientInstanceGetLocalPlayer", false},
};

}  // namespace

CameraFacingModule::CameraFacingModule()
    : Module(std::string(kModuleId), "Camera Facing") {}

std::span<camfacesdk::FactRequirement const> CameraFacingModule::requirements() const {
    return kRequirements;
}

void CameraFacingModule::onEnable() {
    m_captureNext.store(false, std::memory_order_release);
}

void CameraFacingModule::onDisable() {
    m_captureNext.store(false, std::memory_order_release);
    m_lines.clear();
}

void CameraFacingModule::bind(bool hookInstalled, char const* hookStatus, bool rotationReady,
                              char const* rotationReason, std::uint64_t* turnCallCounter) {
    m_hookInstalled = hookInstalled;
    m_hookStatus = hookStatus != nullptr ? hookStatus : "unknown";
    m_rotationReady = rotationReady;
    m_rotationReason = rotationReason != nullptr ? rotationReason : "unknown";
    m_turnCalls = turnCallCounter;
}

void CameraFacingModule::onConfigChanged(std::string_view key, std::string_view value) {
    // "Capture cardinal" is a Button config entry: pressing it marks the next
    // observed orientation for the log, so the reading is tied to what the player
    // was looking at. Name kept stable; it is persisted.
    if (key == "capture") {
        (void)value;
        m_captureNext.store(true, std::memory_order_release);
    }
}

void CameraFacingModule::rebuildLines() {
    m_lines.clear();
    char buffer[160];

    std::snprintf(buffer, sizeof(buffer), "Camera Facing");
    m_lines.emplace_back(buffer);

    // Hook state is the thing T-CAM2 needs to see: a live counter proves the
    // resolved address is the real turn path.
    const std::uint64_t calls = (m_turnCalls != nullptr) ? *m_turnCalls : 0;
    std::snprintf(buffer, sizeof(buffer), "hook: %s", m_hookStatus.c_str());
    m_lines.emplace_back(buffer);

    std::snprintf(buffer, sizeof(buffer), "turn calls: %llu",
                  static_cast<unsigned long long>(calls));
    m_lines.emplace_back(buffer);

    // Orientation reading stays honestly labelled as unavailable until the build
    // data carries a verified accessor. Never invent a value here.
    std::snprintf(buffer, sizeof(buffer), "yaw/pitch: %s",
                  m_rotationReady ? "reading" : m_rotationReason.c_str());
    m_lines.emplace_back(buffer);

    if (m_captureCount != 0) {
        std::snprintf(buffer, sizeof(buffer), "last capture: %s",
                      m_captures[m_captureCount - 1].c_str());
        m_lines.emplace_back(buffer);
    }
}

void CameraFacingModule::onFrame(float deltaSeconds) {
    m_elapsed += deltaSeconds;

    if (m_captureNext.exchange(false, std::memory_order_acq_rel)) {
        // Record the call counter at the moment the user asked. With orientation
        // reading still unavailable this is the honest datum: it timestamps the
        // capture against the live counter, ready for the values to be filled in.
        char buffer[96];
        const std::uint64_t calls = (m_turnCalls != nullptr) ? *m_turnCalls : 0;
        std::snprintf(buffer, sizeof(buffer), "marked @ %llu calls",
                      static_cast<unsigned long long>(calls));
        if (m_captureCount < m_captures.size()) {
            m_captures[m_captureCount++] = buffer;
        }
    }

    // Rebuild at ~10 Hz: cheap, and avoids churning strings every frame.
    if (m_elapsed >= 0.1f) {
        m_elapsed = 0.0f;
        rebuildLines();
    }
}

std::string CameraFacingModule::statusText() const {
    const std::uint64_t calls = (m_turnCalls != nullptr) ? *m_turnCalls : 0;
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "hook=%s turnCalls=%llu rotation=%s", m_hookStatus.c_str(),
                  static_cast<unsigned long long>(calls),
                  m_rotationReady ? "ready" : m_rotationReason.c_str());
    return buffer;
}

}  // namespace camface::features
