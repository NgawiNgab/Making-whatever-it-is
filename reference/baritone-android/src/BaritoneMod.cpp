#include "BaritoneMod.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include "hooks/GameHooks.h"
#include "platform/BaritoneRuntime.h"
#include "platform/Log.h"

namespace baritone {
namespace {

std::mutex gGotoMutex;
std::string gGotoText;

void setGotoText(std::string_view text) {
    std::lock_guard lock(gGotoMutex);
    gGotoText = text;
}

std::string gotoText() {
    std::lock_guard lock(gGotoMutex);
    return gGotoText;
}

}  // namespace

BaritoneMod& BaritoneMod::instance() {
    static BaritoneMod instance;
    return instance;
}

BaritoneMod::BaritoneMod() : mSelf(*ll::mod::NativeMod::current()) {}

bool BaritoneMod::load() {
    auto& self = mSelf;

    std::error_code ec;
    std::filesystem::create_directories(self.getDataDir(), ec);
    if (ec) {
        self.getLogger().error("Failed to create data directory {}: {}",
                               self.getDataDir().string(), ec.message());
        return false;
    }

    std::filesystem::create_directories(self.getConfigDir(), ec);
    if (ec) {
        self.getLogger().error("Failed to create config directory {}: {}",
                               self.getConfigDir().string(), ec.message());
        return false;
    }

    logger::setLogFile(self.getDataDir() / "baritone.log");
    logger::info("Loading Baritone {} (id='{}', build {} {})...", self.getVersion(), self.getId(),
                 __DATE__, __TIME__);
    logger::info("Paths mod='{}' data='{}' config='{}'", self.getModDir().string(),
                 self.getDataDir().string(), self.getConfigDir().string());

    platform::BaritoneRuntime::get().initialize(self.getDataDir());

    pl::modmenu::ModuleBuilder module("baritone", "Baritone");
    module.modId(self.getId())
        .description("Pathfinding bot")
        .defaultEnabled(true)
        .config("goto_coords", "Goal coordinates (x y z or x z)",
                pl::modmenu::ConfigType::Text, "")
        .config("turn_sensitivity", "Turn sensitivity (0.1-10.0, 1=normal)",
                pl::modmenu::ConfigType::Text, "1.0")
        .config("manipulate_camera", "Manipulate camera",
                pl::modmenu::ConfigType::Toggle, "true")
        .config("goto", "Go to coordinates", pl::modmenu::ConfigType::Button)
        .config("test_stop", "Stop", pl::modmenu::ConfigType::Button)
        .onConfigChanged([](std::string_view, std::string_view key, std::string_view value) {
            logger::info("Mod menu config key='{}' value='{}'", key, value);
            if (key.ends_with("goto_coords")) {
                setGotoText(value);
            } else if (key.ends_with("turn_sensitivity")) {
                try {
                    const std::string text(value);
                    std::size_t consumed = 0;
                    const float sensitivity = std::stof(text, &consumed);
                    if (consumed == text.size()) {
                        platform::BaritoneRuntime::get().setTurnSensitivity(sensitivity);
                    }
                } catch (...) {
                }
            } else if (key.ends_with("manipulate_camera")) {
                platform::BaritoneRuntime::get().setManipulateCamera(value == "true" ||
                                                                     value == "1");
            } else if (key.ends_with("goto")) {
                platform::BaritoneRuntime::get().startGoalFromText(gotoText());
            } else if (key.ends_with("test_stop") || key.ends_with("stop")) {
                platform::BaritoneRuntime::get().cancel();
            }
        });
    if (!module.registerModule()) {
        logger::warn("Failed to register the mod menu module");
    } else {
        logger::info("Registered mod menu module 'baritone'");
    }

    logger::info("Loaded {} from {}", self.getName(), self.getModDir().string());
    return true;
}

bool BaritoneMod::enable() {
    auto& self = mSelf;
    logger::setLogFile(self.getDataDir() / "baritone.log");
    logger::info("Enabling Baritone {} from {}...", self.getVersion(), self.getModDir().string());

    platform::BaritoneRuntime::get().initialize(self.getDataDir());
    if (!hooks::install()) {
        logger::error("Failed to install game hooks");
        return false;
    }
    logger::info("Game hooks installed");

    mEnabled = true;
    return true;
}

bool BaritoneMod::disable() {
    if (mEnabled) {
        logger::info("Disabling Baritone...");
        hooks::uninstall();
        platform::BaritoneRuntime::get().shutdown();
        mEnabled = false;
    }
    return true;
}

bool BaritoneMod::unload() {
    logger::info("Unloading Baritone...");
    pl::modmenu::unregisterModule("baritone");
    platform::BaritoneRuntime::get().shutdown();
    return true;
}

}  // namespace baritone
