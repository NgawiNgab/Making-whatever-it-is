#include "Runtime.hpp"

#include "../features/CameraFacingModule.hpp"
#include "../game/RotationSource.hpp"
#include "../game/TurnHook.hpp"
#include "../platform/FramePump.hpp"
#include "../platform/GameImage.hpp"

#include <pl/Logger.hpp>
#include <pl/ModMenu.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace camface::core {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

// The preloader SDK exposes no frame callback, so the swap hook installed by
// FramePump is the clock. This is the same technique the reference uses
// (src/core/GameHooks.cpp:283-292) but through a public libEGL symbol, so it needs
// no game signature and cannot be invalidated by a Minecraft update.
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
    // 1. Identify the build. An unsupported build is a supported *state*: nothing
    //    touches the game and the reason is reported (docs/SPEC.md FR-5).
    camfacesdk::BuildData const& build = camfacesdk::currentBuild();
    log().info("build data {}: librarySize={}", build.mcVersion, build.librarySize);

    // 2. Attach the image. If the game is not mapped, facts stay Unsupported and no
    //    hook is installed; there is deliberately no retry loop.
    m_image = std::make_unique<platform::GameImageSource>();
    const bool attached = m_image->attach();
    if (!attached) log().warn("game image unavailable; launcher-only");

    // 3. Resolve facts: the only step that reads game bytes.
    const bool allAvailable = m_facts.resolveAll(attached ? m_image.get() : nullptr, build);
    log().info("facts {}", m_facts.summary());

    const std::string reportPath =
        (std::filesystem::path(m_configDir) / "resolution-report.json").string();
    m_facts.writeReport(reportPath);
    log().info("report: {}", reportPath);

    // 4. Rotation source. Inert until the data file carries a verified accessor.
    m_rotation = std::make_unique<game::RotationSource>();
    m_rotation->configure(build);

    // 5. The single game hook, installed only from an Available fact.
    m_turnHook = std::make_unique<game::TurnHook>();
    if (camfacesdk::Fact const* fact = m_facts.find("LocalPlayerApplyTurnDelta")) {
        if (fact->available() && attached) {
            const auto address = m_image->toRuntime(fact->rva);
            m_hookStatus = game::TurnHook::describe(
                m_turnHook->install(*fact, m_rotation.get(), address));
        } else {
            m_hookStatus = std::string("fact ") + camfacesdk::toString(fact->state);
        }
    } else {
        m_hookStatus = "no fact in build data";
    }
    if (!m_turnHook->installed()) log().warn("turn hook not installed: {}", m_hookStatus);

    // 6. The feature module. Its gate is real: it opens only if the requirement
    //    list is satisfied, never unconditionally.
    m_cameraFacing = std::make_unique<features::CameraFacingModule>();
    m_cameraFacing->bind(m_turnHook->installed(), m_hookStatus.c_str(), m_rotation->ready(),
                         m_rotation->unavailableReason(),
                         /*turnCallCounter=*/nullptr);

    std::vector<std::string> missing;
    const bool gated = m_facts.satisfies(m_cameraFacing->requirements(), &missing);
    std::string gateReason;
    if (gated) {
        gateReason = "requirements met";
    } else {
        gateReason = "missing: ";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i != 0) gateReason += ", ";
            gateReason += missing[i];
        }
    }
    m_cameraFacing->setGate(gated, gateReason);
    log().info("module {} gate={} ({})", m_cameraFacing->id(), gated ? "open" : "closed",
               gateReason);

    registerModule();

    // 7. Frame clock last, so a failure here leaves the diagnostic state intact.
    if (!platform::FramePump::start(&onFrameTick)) {
        log().warn("no frame clock; readout will not refresh");
    }

    if (!allAvailable) m_cameraFacing->onDisable();
}

void Runtime::registerModule() {
    pl::modmenu::ModuleInfo info;
    info.moduleId = features::CameraFacingModule::kModuleId;
    info.displayName = "Camera Facing";
    info.description = "Observe and snap the view to a cardinal yaw.";
    info.modId = m_modId;
    info.defaultEnabled = false;

    // Without these two callbacks the launcher would own the toggle state and the
    // module would never learn about it: the config button below would be inert.
    info.onToggle = [this](std::string_view moduleId, bool enabled) {
        if (moduleId != features::CameraFacingModule::kModuleId) return;
        m_moduleEnabled = enabled;
        if (!m_cameraFacing) return;
        if (enabled && !m_cameraFacing->gateOpen()) {
            log().warn("toggle ignored: gate closed ({})", m_cameraFacing->gateReason());
            m_moduleEnabled = false;
            return;
        }
        if (enabled) {
            m_cameraFacing->onEnable();
        } else {
            m_cameraFacing->onDisable();
            pl::modmenu::submitDrawCommands(features::CameraFacingModule::kModuleId, {});
        }
    };

    info.onConfigChanged = [this](std::string_view moduleId, std::string_view key,
                                  std::string_view value) {
        if (moduleId != features::CameraFacingModule::kModuleId) return;
        if (m_cameraFacing) m_cameraFacing->onConfigChanged(key, value);
    };

    pl::modmenu::ConfigEntry capture;
    capture.key = "capture";
    capture.displayName = "Mark reading";
    capture.type = pl::modmenu::ConfigType::Button;
    info.configs.push_back(std::move(capture));

    if (!pl::modmenu::registerModule(info)) {
        log().error("module registration failed");
    }
}

void Runtime::onFrame(float deltaSeconds) {
    if (!m_enabled || !m_cameraFacing || !m_moduleEnabled) return;

    m_cameraFacing->onFrame(deltaSeconds);

    const auto& lines = m_cameraFacing->drawLines();
    if (lines.empty()) return;

    std::vector<pl::modmenu::DrawCommand> commands;
    commands.reserve(lines.size());
    float y = 24.0f;
    for (auto const& line : lines) {
        pl::modmenu::DrawCommand command;
        command.type = pl::modmenu::DrawCommandType::Text;
        command.x = 24.0f;
        command.y = y;
        command.w = 460.0f;
        command.h = 22.0f;
        command.size = 18.0f;
        command.color = 0xFFFFFFFFu;  // ARGB opaque white
        command.text = line;
        commands.push_back(std::move(command));
        y += 22.0f;
    }
    pl::modmenu::submitDrawCommands(features::CameraFacingModule::kModuleId, commands);
}

bool Runtime::disable(pl::mod::ModContext&) {
    platform::FramePump::stop();
    if (m_turnHook) m_turnHook->remove();
    if (m_cameraFacing) {
        m_cameraFacing->onDisable();
        pl::modmenu::submitDrawCommands(features::CameraFacingModule::kModuleId, {});
    }
    m_enabled = false;
    log().info("disabled; hooks removed");
    return true;
}

bool Runtime::unload(pl::mod::ModContext&) {
    platform::FramePump::stop();
    if (m_turnHook) m_turnHook->remove();
    pl::modmenu::unregisterModule(features::CameraFacingModule::kModuleId);
    m_turnHook.reset();
    m_rotation.reset();
    m_cameraFacing.reset();
    m_image.reset();
    m_enabled = false;
    return true;
}

}  // namespace camface::core
