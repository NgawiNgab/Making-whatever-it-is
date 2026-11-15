#include "RotationSource.hpp"

#include <pl/Logger.hpp>

namespace camface::game {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

// BedrockTools: include/bedrocktools/sdk/offsets/World.hpp:17
//
// NOTE ON A DESIGN MISTAKE I MADE HERE FIRST: I originally resolved "is an accessor
// verified?" by looking for an OffsetEntry in the build data and refusing to read
// anything until one existed. That was circular -- the data file only gets an entry
// after a measurement, and the measurement needed the read. The readout could never
// show a number. The fix is to read with the reference's offsets, label the result
// UNVERIFIED, and let the measurement confirm or refute it.
constexpr std::size_t kActorRotationComponent = 0x238;  // a pointer, not the floats

// Reads a pointer from inside the game image. Kept deliberately raw: this is the one
// place that dereferences game memory, so it lives alone and is easy to audit.
void* readPointer(void* address) {
    if (address == nullptr) return nullptr;
    return *reinterpret_cast<void**>(address);
}

}  // namespace

void RotationSource::setClientInstance(void* clientInstance) {
    if (clientInstance == nullptr) return;
    m_clientInstance = clientInstance;
    m_state = "client instance captured";
}

void RotationSource::setLocalPlayerGetter(std::uintptr_t getLocalPlayerRva) {
    m_getLocalPlayer = getLocalPlayerRva;
    if (m_getLocalPlayer == 0) m_state = "local-player getter unresolved";
}

void RotationSource::refreshLocalPlayer() {
    if (m_clientInstance == nullptr) {
        m_localPlayer = nullptr;
        m_state = "no client instance yet";
        return;
    }
    if (m_getLocalPlayer == 0) {
        m_state = "local-player getter unresolved";
        return;
    }

    using GetLocalPlayer = void* (*)(void*);
    const auto function = reinterpret_cast<GetLocalPlayer>(m_getLocalPlayer);
    // A signature that resolves to the wrong function is a crash, not an exception;
    // there is nothing to catch here. The gate above (fact must be AVAILABLE) and the
    // per-frame null check are what keep a wrong address from being called blindly.
    void* player = function(m_clientInstance);

    m_localPlayer = player;
    m_state = player != nullptr ? "reading" : "local player is null";
}

bool RotationSource::ready() const noexcept {
    return m_localPlayer != nullptr && m_getLocalPlayer != 0;
}

Orientation RotationSource::read() const {
    Orientation orientation;
    if (m_localPlayer == nullptr) return orientation;

    auto* component = readPointer(static_cast<char*>(m_localPlayer) + kActorRotationComponent);
    if (component == nullptr) return orientation;

    // Vec2 { float x; float y; } at component + 0 -- BedrockTools Types.hpp:7 and
    // Actor::rotation(). x is pitch, y is yaw (compass.cpp reads rotation.y as yaw).
    orientation.pitch = *reinterpret_cast<const float*>(static_cast<const char*>(component) + 0);
    orientation.yaw = *reinterpret_cast<const float*>(static_cast<const char*>(component) + 4);
    orientation.valid = true;
    return orientation;
}

}  // namespace camface::game
