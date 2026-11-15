#pragma once
// Test-only PlayerContext: a fake player standing in a generated world.
//
// This is NOT part of the mod. It exists so MineProcess and the pathfinder can be
// run and measured on a desktop instead of guessed at on a phone.

#include <cmath>
#include <string>
#include <vector>

#include "baritone/api/PlayerContext.h"
#include "tests/TestWorld.h"

namespace baritone_test {

class TestPlayer : public baritone::PlayerContext {
public:
    // --- what the test controls ---
    void setPosition(const baritone::Vec3d& p) { pos_ = p; x_ = p.x; y_ = p.y; z_ = p.z; }
    void setRotation(const baritone::Rotation& r) { rot_ = r; }
    void setGroundY(double y) { groundY_ = y; y_ = y; }

    // Stand in for the game: take the controls the bot produced and move this player
    // the way Bedrock would. Without this the bot aims but never arrives.
    void applyControls(const baritone::MovementState& c) {
        if (c.target.rotation) rot_ = *c.target.rotation;

        double fwd = 0, strafe = 0;
        if (c.input(baritone::Input::MoveForward)) fwd += 1;
        if (c.input(baritone::Input::MoveBack)) fwd -= 1;
        if (c.input(baritone::Input::MoveLeft)) strafe -= 1;
        if (c.input(baritone::Input::MoveRight)) strafe += 1;

        const double speed = c.input(baritone::Input::Sprint) ? 0.28 : 0.215;
        const double yawRad = rot_.yaw * 3.14159265358979323846 / 180.0;
        // Bedrock: forward is -Z rotated by yaw
        const double dx = (-std::sin(yawRad) * fwd + std::cos(yawRad) * strafe) * speed;
        const double dz = (std::cos(yawRad) * fwd + std::sin(yawRad) * strafe) * speed;
        x_ += dx;
        z_ += dz;

        if (c.input(baritone::Input::Jump)) y_ += 0.42;
        if (y_ > groundY_) {
            y_ -= 0.08;  // crude gravity
            if (y_ < groundY_) y_ = groundY_;
        }
        pos_ = {x_, y_, z_};
    }

    // --- PlayerContext ---
    bool valid() const override { return true; }
    baritone::Vec3d position() const override { return pos_; }
    baritone::Vec3d eyePosition() const override { return {pos_.x, pos_.y + 1.62, pos_.z}; }
    baritone::Vec3d velocity() const override { return {}; }
    baritone::Rotation rotation() const override { return rot_; }
    bool onGround() const override { return true; }
    bool isSneaking() const override { return false; }
    bool isInWater() const override { return false; }
    double blockReachDistance() const override { return 4.5; }

    int selectedSlot() const override { return slot_; }
    void selectSlot(int slot) override { slot_ = slot; }
    baritone::HotbarStack hotbar(int slot) const override {
        if (slot == 0) {
            baritone::HotbarStack s;
            s.name = "minecraft:iron_pickaxe";
            s.count = 1;
            s.tool = baritone::Tool::Pickaxe;
            s.toolTier = 3;
            s.toolSpeed = 6.0f;
            s.maxDamage = 250;
            return s;
        }
        return {};
    }
    int countItem(std::string_view) const override { return 0; }
    std::vector<baritone::EntityInfo> entities() const override { return {}; }

    bool startOrContinueBreaking(const baritone::BlockPos& pos, baritone::Direction) override {
        breakRequests.push_back(pos);
        return breakRequests.size() > 1;  // "broken" after a couple of hits
    }
    void stopBreaking() override {}
    void useItemOn(const baritone::BlockPos&, baritone::Direction, const baritone::Vec3d&) override {}
    void swingArm() override {}

    void refreshWorld(const baritone::BlockPos&, int) override {}

    // --- what the test observes ---
    std::vector<baritone::BlockPos> breakRequests;

private:
    baritone::Vec3d pos_{0, 0, 0};
    baritone::Rotation rot_{};
    int slot_ = 0;
    double x_ = 0, y_ = 0, z_ = 0;
    double groundY_ = 0;
};

}  // namespace baritone_test
