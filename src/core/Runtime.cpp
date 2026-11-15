#include "Runtime.hpp"

#include "../features/CameraFacingModule.hpp"
#include "../game/GameHooks.hpp"
#include "../game/RotationSource.hpp"
#include "../platform/FramePump.hpp"
#include "../platform/GameImage.hpp"

#include <pl/Logger.hpp>
#include <pl/ModMenu.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace camface::core {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

// The preloader SDK has no frame callback, so the tick comes from eglSwapBuffers --
// a public libEGL symbol, needing no game signature. Same technique as
// BedrockTools src/core/GameHooks.cpp:283-292.
void onFrameTick(float deltaSeconds) { Runtime::get().onFrame(deltaSeconds); }

}  // namespace

Runtime& Runtime::get() {
    static Runtime runtime;
    return runtime;
}

bool Runtime::load(pl::mod::ModContext& context) {
    m_modId = context.id();
    m_configDir = context.configDir().string();
    log().info("loaded id={} version={}", m_modId, context.info().version);
    return true;
}

bool Runtime::enable(pl::mod::ModContext&) {
    m_enabled = true;
    if (!m_attempted) {
        m_attempted = true;
        installFeatures();
    }
    return true;
}

void Runtime::installFeatures() {
    camfacesdk::BuildData const& build = camfacesdk::currentBuild();
    log().info("build data {}: librarySize={}", build.mcVersion, build.librarySize);

    m_image = std::make_unique<platform::GameImageSource>();
    const bool attached = m_image->attach();
    if (!attached) log().warn("game image unavailable; launcher-only");

    m_facts.resolveAll(attached ? m_image.get() : nullptr, build);
    log().info("facts {}", m_facts.summary());
    m_facts.writeReport((std::filesystem::path(m_configDir) / "resolution-report.json").string());

    m_rotation = std::make_unique<game::RotationSource>();

    // Resolve the three addresses the readout needs. Each is used only if its fact is
    // Available, which already implies the signature matched exactly once and the
    // structural checks passed.
    game::GameHooks::Targets targets;
    auto addressOf = [this](char const* id, std::size_t* sizeOut) -> std::uintptr_t {
        camfacesdk::Fact const* fact = m_facts.find(id);
        if (fact == nullptr || !fact->available() || !m_image) return 0;
        if (sizeOut != nullptr) *sizeOut = fact->minSize;
        return m_image->toRuntime(fact->rva);
    };

    targets.clientInstanceUpdate = addressOf("ClientInstanceUpdate", &targets.clientInstanceUpdateSize);
    targets.normalTick = addressOf("NormalTick", &targets.normalTickSize);
    targets.getLocalPlayer = addressOf("ClientInstanceGetLocalPlayer", nullptr);

    m_rotation->setLocalPlayerGetter(targets.getLocalPlayer);

    const auto hookResult = game::GameHooks::install(targets, m_rotation.get());
    switch (hookResult) {
        case game::GameHooks::Result::Installed: m_hookStatus = "installed"; break;
        case game::GameHooks::Result::FactUnavailable: m_hookStatus = "fact unavailable"; break;
        case game::GameHooks::Result::TargetTooSmall: m_hookStatus = "target too small"; break;
        case game::GameHooks::Result::EngineRefused: m_hookStatus = "engine refused"; break;
        case game::GameHooks::Result::VerifyFailed: m_hookStatus = "verification failed"; break;
    }
    const bool hooksInstalled = hookResult == game::GameHooks::Result::Installed;
    if (!hooksInstalled) log().warn("hooks not installed: {}", m_hookStatus);

    m_cameraFacing = std::make_unique<features::CameraFacingModule>();
    m_cameraFacing->bind(m_rotation.get(), m_hookStatus.c_str(), hooksInstalled);

    std::vector<std::string> missing;
    const bool gated = m_facts.satisfies(m_cameraFacing->requirements(), &missing);
    std::string gateReason = gated ? "requirements met" : "missing: ";
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (i != 0) gateReason += ", ";
        gateReason += missing[i];
    }
    m_cameraFacing->setGate(gated, gateReason);
    log().info("module gate={} ({})", gated ? "open" : "closed", gateReason);

    registerModule();

    if (!platform::FramePump::start(&onFrameTick)) {
        log().warn("no frame clock; the readout will not refresh");
    }
}

void Runtime::registerModule() {
    pl::modmenu::ModuleInfo info;
    info.moduleId = features::CameraFacingModule::kModuleId;
    info.displayName = "Camera Facing";
    info.description = "Live yaw readout; marks readings for measuring the yaw convention.";
    info.modId = m_modId;
    // On by default: the user should not have to find a toggle to see whether the
    // mod works at all. It only draws, so there is nothing unsafe about that.
    info.defaultEnabled = true;

    // These two callbacks are what the launcher actually calls. Without them the
    // module never hears about a toggle, and it is never enabled -- which is one of
    // the reasons nothing appeared on screen.
    info.onToggle = [this](std::string_view moduleId, bool enabled) {
        if (moduleId != features::CameraFacingModule::kModuleId) return;
        m_moduleEnabled = enabled;
        if (!m_cameraFacing) return;
        if (enabled) {
            m_cameraFacing->onEnable();
        } else {
            m_cameraFacing->onDisable();
        }
    };

    info.onConfigChanged = [this](std::string_view moduleId, std::string_view key,
                                  std::string_view value) {
        if (moduleId != features::CameraFacingModule::kModuleId) return;
        if (m_cameraFacing) m_cameraFacing->onConfigChanged(key, value);
    };

    pl::modmenu::ConfigEntry mark;
    mark.key = "mark";
    mark.displayName = "Mark reading";
    mark.type = pl::modmenu::ConfigType::Button;
    info.configs.push_back(std::move(mark));

    // Position entries so the HUD editor can move the block. Keys match
    // HudEditorElement::positionKeyX/Y defaults, which is what the editor reads.
    pl::modmenu::ConfigEntry posX;
    posX.key = "hudPosX";
    posX.displayName = "HUD X";
    posX.type = pl::modmenu::ConfigType::SliderFloat;
    posX.defaultValue = "20";
    posX.minValue = "0";
    posX.maxValue = "2000";
    info.configs.push_back(std::move(posX));

    pl::modmenu::ConfigEntry posY;
    posY.key = "hudPosY";
    posY.displayName = "HUD Y";
    posY.type = pl::modmenu::ConfigType::SliderFloat;
    posY.defaultValue = "120";
    posY.minValue = "0";
    posY.maxValue = "2000";
    info.configs.push_back(std::move(posY));

    if (!pl::modmenu::registerModule(info)) {
        log().error("module registration failed");
        return;
    }
    m_moduleEnabled = true;
    if (m_cameraFacing) m_cameraFacing->onEnable();
}

void Runtime::onFrame(float deltaSeconds) {
    if (!m_enabled || !m_cameraFacing || !m_moduleEnabled) return;
    m_cameraFacing->onFrame(deltaSeconds);
}

bool Runtime::disable(pl::mod::ModContext&) {
    platform::FramePump::stop();
    game::GameHooks::remove();
    if (m_cameraFacing) m_cameraFacing->onDisable();
    m_enabled = false;
    log().info("disabled; hooks removed");
    return true;
}

bool Runtime::unload(pl::mod::ModContext&) {
    platform::FramePump::stop();
    game::GameHooks::remove();
    pl::modmenu::unregisterModule(features::CameraFacingModule::kModuleId);
    m_cameraFacing.reset();
    m_rotation.reset();
    m_image.reset();
    m_enabled = false;
    return true;
}

}  // namespace camface::core
