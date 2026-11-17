#include "sim/player.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "baritone/cache/WorldScanner.h"
#include "baritone/utils/RayTrace.h"
#include "baritone/world/BlockStateInterface.h"

namespace mwi::sim {
namespace {

baritone::Tool toolFromString(const std::string& name) {
    if (name == "pickaxe") return baritone::Tool::Pickaxe;
    if (name == "axe") return baritone::Tool::Axe;
    if (name == "shovel") return baritone::Tool::Shovel;
    if (name == "hoe") return baritone::Tool::Hoe;
    if (name == "sword") return baritone::Tool::Sword;
    if (name == "shears") return baritone::Tool::Shears;
    return baritone::Tool::None;
}

const char* faceName(baritone::Direction face) {
    switch (face) {
        case baritone::Direction::Down: return "down";
        case baritone::Direction::Up: return "up";
        case baritone::Direction::North: return "north";
        case baritone::Direction::South: return "south";
        case baritone::Direction::West: return "west";
        case baritone::Direction::East: return "east";
    }
    return "unknown";
}

constexpr double kDeg = std::numbers::pi / 180.0;

}  // namespace

HeadlessPlayer::HeadlessPlayer(const Scenario& scenario, World& world,
                               const baritone::BlockFilter& filter)
    : scenario_(scenario), world_(world), filter_(filter) {
    setPosition({scenario.player.x, scenario.player.y, scenario.player.z});
    rotation_ = {static_cast<float>(scenario.yaw), static_cast<float>(scenario.pitch)};
    reach_ = scenario.reach;
    breakTicks_ = scenario.breakTicks;
    for (std::size_t i = 0; i < scenario.hotbar.size() && i < hotbar_.size(); ++i) {
        const HotbarItem& item = scenario.hotbar[i];
        baritone::HotbarStack stack;
        stack.name = item.name;
        stack.count = item.count;
        stack.tool = toolFromString(item.tool);
        stack.toolTier = item.toolTier;
        stack.toolSpeed = static_cast<float>(item.toolSpeed);
        stack.maxDamage = item.maxDamage;
        stack.maxStackSize = 64;
        hotbar_[i] = std::move(stack);
    }
    inventory_ = scenario.inventory;
}

void HeadlessPlayer::setPosition(const baritone::Vec3d& position) {
    x_ = position.x;
    y_ = position.y;
    z_ = position.z;
    // Gravity in applyControls() pulls towards groundY_. A spawn Y that is not the
    // resolved footing (baritone lifts the bot out of a solid block before the first
    // tick) would otherwise drop the player through the floor for the whole run.
    groundY_ = position.y;
}

double HeadlessPlayer::footingY(int x, int z) const {
    for (int y = static_cast<int>(std::floor(y_)) + 2; y >= scenario_.minY; --y) {
        if (world_.isSolid(x, y, z)) return static_cast<double>(y + 1);
    }
    return static_cast<double>(scenario_.groundY);
}

void HeadlessPlayer::applyControls(const baritone::MovementState& controls) {
    using baritone::Input;
    if (controls.target.rotation) rotation_ = *controls.target.rotation;

    double forward = 0.0, strafe = 0.0;
    if (controls.input(Input::MoveForward)) forward += 1.0;
    if (controls.input(Input::MoveBack)) forward -= 1.0;
    if (controls.input(Input::MoveLeft)) strafe -= 1.0;
    if (controls.input(Input::MoveRight)) strafe += 1.0;

    const double speed = controls.input(Input::Sprint) ? 0.28 : 0.215;
    const double yawRad = rotation_.yaw * kDeg;
    const double dx = (-std::sin(yawRad) * forward + std::cos(yawRad) * strafe) * speed;
    const double dz = (std::cos(yawRad) * forward + std::sin(yawRad) * strafe) * speed;
    constexpr double kRadius = 0.3;
    if (canStand(x_ + dx, z_, kRadius)) x_ += dx;
    if (canStand(x_, z_ + dz, kRadius)) z_ += dz;

    if (controls.input(Input::Jump)) y_ += 0.42;
    if (y_ > groundY_) {
        y_ -= 0.08;
        if (y_ < groundY_) y_ = groundY_;
    }

    if (controls.input(Input::ClickLeft)) handleClick();
}

bool HeadlessPlayer::canStand(double newX, double newZ, double radius) const {
    const int feetY = static_cast<int>(std::floor(y_));
    const int headY = static_cast<int>(std::floor(y_ + 1.62));
    for (int y : {feetY, headY}) {
        for (double cx : {newX - radius, newX + radius}) {
            for (double cz : {newZ - radius, newZ + radius}) {
                if (world_.isSolid(static_cast<int>(std::floor(cx)), y,
                                   static_cast<int>(std::floor(cz)))) {
                    return false;
                }
            }
        }
    }
    return true;
}

const baritone::HotbarStack& HeadlessPlayer::selectedStack() const {
    return hotbar_[static_cast<std::size_t>(selectedSlot_)];
}

void HeadlessPlayer::handleClick() {
    ++metrics_.clicks;
    // Use the core's rayTrace, not platform/AimRay.h: the latter uses yaw 0 = -Z
    // while the core uses yaw 0 = +Z. Mixing them would break the wrong block.
    baritone::BlockStateInterface bsi(world_.snapshot());
    const auto hit = baritone::rayTrace(bsi, eyePosition(), rotation_, reach_);
    if (!hit) {
        ++metrics_.unresolvedClicks;
        ++metrics_.falsePositiveSuspects;
        return;
    }
    (void)startOrContinueBreaking(hit->pos, hit->face);
}

bool HeadlessPlayer::collectedEnough(int quantity) const {
    if (quantity <= 0) return false;
    int total = 0;
    for (const std::string& name : filter_.dropNames()) total += countItem(name);
    return total >= quantity;
}

bool HeadlessPlayer::valid() const { return true; }
baritone::Vec3d HeadlessPlayer::position() const { return {x_, y_, z_}; }
baritone::Vec3d HeadlessPlayer::eyePosition() const { return {x_, y_ + 1.62, z_}; }
baritone::Vec3d HeadlessPlayer::velocity() const { return {}; }
baritone::Rotation HeadlessPlayer::rotation() const { return rotation_; }
bool HeadlessPlayer::onGround() const { return true; }
bool HeadlessPlayer::isSneaking() const { return false; }
bool HeadlessPlayer::isInWater() const { return false; }
double HeadlessPlayer::blockReachDistance() const { return reach_; }

int HeadlessPlayer::selectedSlot() const { return selectedSlot_; }
void HeadlessPlayer::selectSlot(int slot) {
    selectedSlot_ = std::clamp(slot, 0, static_cast<int>(hotbar_.size()) - 1);
}
baritone::HotbarStack HeadlessPlayer::hotbar(int slot) const {
    if (slot < 0 || slot >= static_cast<int>(hotbar_.size())) return {};
    return hotbar_[static_cast<std::size_t>(slot)];
}

int HeadlessPlayer::countItem(std::string_view name) const {
    int total = 0;
    if (auto it = inventory_.find(std::string(name)); it != inventory_.end()) total += it->second;
    for (const baritone::HotbarStack& stack : hotbar_) {
        if (!stack.empty() && stack.name == name) total += stack.count;
    }
    return total;
}

std::vector<baritone::EntityInfo> HeadlessPlayer::entities() const { return {}; }

bool HeadlessPlayer::startOrContinueBreaking(const baritone::BlockPos& pos,
                                             baritone::Direction face) {
    ++metrics_.contextBreakCalls;
    int& progress = breakProgress_[pos];
    ++progress;
    if (progress < breakTicks_) return false;

    const baritone::BlockProps& before = world_.props(pos);
    const std::string block = before.name;
    const bool targetHit = filter_.matches(before);
    const DropDecision drop = world_.dropFor(pos, selectedStack());

    if (!drop.breakable) {
        ++metrics_.refusedBreaks;
        ++metrics_.falsePositiveSuspects;
        breakProgress_.erase(pos);
        return false;
    }

    world_.setAir(pos);
    breakProgress_.erase(pos);
    if (!drop.item.empty()) {
        inventory_[drop.item] += 1;
        ++metrics_.dropsCollected;
    }
    if (targetHit) ++metrics_.targetBlocksBroken;
    else ++metrics_.obstacleBlocksBroken;

    breakEvents_.push_back(BreakEvent{
        tick_, pos.x, pos.y, pos.z, block, faceName(face), drop.item, targetHit,
        drop.toolSatisfied, drop.note, breakTicks_,
    });
    return true;
}

void HeadlessPlayer::stopBreaking() { breakProgress_.clear(); }
void HeadlessPlayer::useItemOn(const baritone::BlockPos&, baritone::Direction,
                               const baritone::Vec3d&) {}
void HeadlessPlayer::swingArm() {}
void HeadlessPlayer::refreshWorld(const baritone::BlockPos&, int) {}

}  // namespace mwi::sim
