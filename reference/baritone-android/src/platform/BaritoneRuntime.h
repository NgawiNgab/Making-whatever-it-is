#pragma once
#include <atomic>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include "baritone/pathing/movement/Movement.h"

namespace baritone {
class Baritone;
}

namespace baritone::platform {

class GamePlayerContext;
class InputOverride;
class PathVisualizer;

// Owns the bot and the live player context. Called from the ClientInputUpdateSystem tick:
// beforeGameTick applies the controls produced by the previous tick, afterGameTick
// refreshes the world snapshot and runs Baritone.
class BaritoneRuntime {
public:
    BaritoneRuntime();
    ~BaritoneRuntime();

    static BaritoneRuntime& get();

    void initialize(const std::filesystem::path& dataDir);
    void shutdown();

    void beforeGameTick(void* registry);
    void afterGameTick(void* registry);

    // Test entry points for the mod menu (no chat command bridge yet).
    void startTestGoal();
    void startGoalFromText(std::string_view text);
    // #mine: find the closest blocks matching `filterText` (a block name such as
    // "sugarcane" or "iron_ore"), then path to and break them. `quantity` of 0 keeps
    // going; a positive number stops once that many are collected.
    void startMineFromText(std::string_view filterText, int quantity);
    void setFovCompensation(float degrees);
    void setTurnSensitivity(float sensitivity);
    void setManipulateCamera(bool enabled);
    void cancel();

    PathVisualizer* visualizer() { return visualizer_.get(); }

private:
    struct GoalRequest {
        int x = 0;
        int y = 0;
        int z = 0;
        bool hasY = false;
    };

    std::unique_ptr<GamePlayerContext> context_;
    std::unique_ptr<baritone::Baritone> baritone_;
    std::unique_ptr<InputOverride> input_;
    std::unique_ptr<PathVisualizer> visualizer_;
    baritone::MovementState lastControls_{};
    std::bitset<static_cast<std::size_t>(baritone::Input::Count)> lastInputs_{};
    std::uint64_t ticks_ = 0;
    std::uint64_t skippedTicks_ = 0;
    bool initialized_ = false;
    bool firstTickLogged_ = false;
    bool haveLastInputs_ = false;
    bool lastProduced_ = false;
    void* lastRegistry_ = nullptr;
    void* lastActor_ = nullptr;
    void* lastBlockSource_ = nullptr;
    std::atomic<bool> pendingTestGoal_{false};
    std::atomic<bool> pendingCancel_{false};
    std::atomic<float> fovCompensation_{0.0f};
    std::atomic<float> turnSensitivity_{1.0f};
    std::atomic<bool> manipulateCamera_{true};
    // Parsed #mine request, handed to MineProcess on the game thread.
    struct MineRequest {
        std::string names;      // comma separated block names, normalized
        int quantity = 0;
    };

    std::mutex goalMutex_;
    std::optional<GoalRequest> pendingGoal_;
    std::optional<MineRequest> pendingMine_;
    // Held for one tick so MineProcess::mine() runs against a world snapshot.
    std::optional<MineRequest> pendingMineNextTick_;
};

}  // namespace baritone::platform
