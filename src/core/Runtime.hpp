#pragma once

#include <camfacesdk/Facts.hpp>

#include <pl/Mod.hpp>

#include <memory>
#include <string>
#include <vector>

namespace camface::platform {
class GameImageSource;
}

namespace camface::game {
class GameHooks;
class RotationSource;
}

namespace camface::features {
class CameraFacingModule;
}

namespace camface::core {

// Owns the mod's lifecycle and the one ordered startup path:
//   identify build -> attach image -> resolve facts -> gate modules -> install hooks
//
// Every step is observable: the resolution report and the install log are the
// deliverables, not side effects (docs/SPEC.md FR-2).
class Runtime {
public:
    static Runtime& get();

    bool load(pl::mod::ModContext& context);
    bool enable(pl::mod::ModContext& context);
    bool disable(pl::mod::ModContext& context);
    bool unload(pl::mod::ModContext& context);

    // Called from the frame pump. Cheap; the module throttles its own work.
    void onFrame(float deltaSeconds);

    [[nodiscard]] std::string const& modId() const noexcept { return m_modId; }

private:
    void installFeatures();
    void registerModule();

    std::string m_modId;
    std::string m_configDir;
    bool m_enabled = false;
    bool m_attempted = false;
    bool m_moduleEnabled = false;

    camfacesdk::FactRegistry m_facts;
    std::string m_startupReport;
    std::string m_hookStatus = "not attempted";  // owned: kept alive for bind()

    std::unique_ptr<platform::GameImageSource> m_image;
    std::unique_ptr<game::RotationSource> m_rotation;

    std::unique_ptr<features::CameraFacingModule> m_cameraFacing;
};

}  // namespace camface::core
