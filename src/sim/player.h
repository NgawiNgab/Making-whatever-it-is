#pragma once
// Deterministic PlayerContext for the headless engine.
//
// The vendored core emits Input::ClickLeft plus a target Rotation. This adapter
// closes that gap with the core's OWN rayTrace(), so yaw/pitch conventions match
// exactly; it then mutates the in-memory WorldSnapshot. It is not a game hook.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "baritone/api/PlayerContext.h"
#include "baritone/pathing/movement/Movement.h"
#include "baritone/utils/BlockPos.h"
#include "baritone/utils/Rotation.h"
#include "sim/scenario.h"
#include "sim/world.h"

namespace baritone {
class BlockFilter;
}

namespace mwi::sim {

struct BreakEvent {
    int tick = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    std::string block;
    std::string face;
    std::string drop;
    bool targetHit = false;
    bool toolSatisfied = true;
    std::string note;
    int clicks = 1;
};

struct PlayerMetrics {
    std::uint64_t clicks = 0;
    std::uint64_t unresolvedClicks = 0;
    std::uint64_t refusedBreaks = 0;
    std::uint64_t contextBreakCalls = 0;
    std::uint64_t targetBlocksBroken = 0;
    std::uint64_t obstacleBlocksBroken = 0;
    std::uint64_t dropsCollected = 0;
    std::uint64_t falsePositiveSuspects = 0;
};

class HeadlessPlayer final : public baritone::PlayerContext {
public:
    HeadlessPlayer(const Scenario& scenario, World& world, const baritone::BlockFilter& filter);

    void setTick(int tick) { tick_ = tick; }
    void setPosition(const baritone::Vec3d& position);
    // Ground height under (x, z): the top of the highest solid block below the feet.
    // Used once, after the core first resolves the bot's footing.
    double footingY(int x, int z) const;
    void setGroundY(double y) { groundY_ = y; }
    double groundY() const { return groundY_; }
    void setRotation(const baritone::Rotation& rotation) { rotation_ = rotation; }
    void applyControls(const baritone::MovementState& controls);

    bool collectedEnough(int quantity) const;
    const std::map<std::string, int>& inventorySnapshot() const { return inventory_; }
    const std::vector<BreakEvent>& breakEvents() const { return breakEvents_; }
    const PlayerMetrics& metrics() const { return metrics_; }

    // --- baritone::PlayerContext ---
    bool valid() const override;
    baritone::Vec3d position() const override;
    baritone::Vec3d eyePosition() const override;
    baritone::Vec3d velocity() const override;
    baritone::Rotation rotation() const override;
    bool onGround() const override;
    bool isSneaking() const override;
    bool isInWater() const override;
    double blockReachDistance() const override;

    int selectedSlot() const override;
    void selectSlot(int slot) override;
    baritone::HotbarStack hotbar(int slot) const override;
    int countItem(std::string_view name) const override;
    std::vector<baritone::EntityInfo> entities() const override;

    bool startOrContinueBreaking(const baritone::BlockPos& pos, baritone::Direction face) override;
    void stopBreaking() override;
    void useItemOn(const baritone::BlockPos& pos, baritone::Direction face,
                   const baritone::Vec3d& hit) override;
    void swingArm() override;
    void refreshWorld(const baritone::BlockPos& center, int radius) override;

private:
    bool canStand(double newX, double newZ, double radius) const;
    const baritone::HotbarStack& selectedStack() const;
    void handleClick();

    Scenario scenario_;
    World& world_;
    const baritone::BlockFilter& filter_;
    int tick_ = 0;
    double x_ = 0.0, y_ = 0.0, z_ = 0.0;
    double groundY_ = 0.0;
    double reach_ = 4.5;
    int breakTicks_ = 1;
    int selectedSlot_ = 0;
    baritone::Rotation rotation_{};
    std::array<baritone::HotbarStack, 36> hotbar_{};
    std::map<std::string, int> inventory_;
    std::map<baritone::BlockPos, int> breakProgress_;
    std::vector<BreakEvent> breakEvents_;
    PlayerMetrics metrics_{};
};

}  // namespace mwi::sim
