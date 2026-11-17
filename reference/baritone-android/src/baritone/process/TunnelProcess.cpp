#include "baritone/process/TunnelProcess.h"

#include <cmath>
#include <format>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/pathing/movement/CalculationContext.h"
#include "baritone/pathing/movement/MovementHelper.h"

namespace baritone {

namespace {

// Distance travelled along the tunnel axis.
int alongAxis(const BlockPos& from, const BlockPos& to, Direction dir) {
    const DirOffset off = offsetOf(dir);
    return (to.x - from.x) * off.x + (to.z - from.z) * off.z;
}

const char* toName(Direction d) {
    switch (d) {
        case Direction::North: return "north";
        case Direction::South: return "south";
        case Direction::West: return "west";
        case Direction::East: return "east";
        default: return "up";
    }
}

}  // namespace

Direction nearestHorizontal(float yaw) {
    // Core yaw convention (Rotation.h / calcLookDirection): 0 = +Z (south),
    // 90 = -X (west), 180 = -Z (north), 270 = +X (east). This previously had
    // north/south swapped; Orientation.cpp already used the correct mapping.
    float y = std::fmod(yaw + 360.0f, 360.0f);
    if (y >= 315.0f || y < 45.0f) return Direction::South;
    if (y < 135.0f) return Direction::West;
    if (y < 225.0f) return Direction::North;
    return Direction::East;
}

void TunnelProcess::start(Direction direction, int length) {
    onLostControl();
    direction_ = direction;
    length_ = length;
    start_ = baritone_.playerFeet();
    lastFeet_ = start_;
    stalled_ = 0;
    announced_ = false;
    active_ = true;

    baritone_.logDirect(std::format("Tunnelling {} for {}", toName(direction),
                                    length > 0 ? std::to_string(length) + " blocks"
                                               : std::string("as long as it goes")),
                        false);
}

// Which two blocks the corridor needs cleared at the current step.
//
// The bot stands in a 1x2 space. To move forward one block, the block in front at feet
// level and the block in front at head level both have to be air. Movement only lists
// the feet-level one (that is what stops the player's box), so the head block is this
// process's job - without it the bot walks into a 1-high gap and stops.
std::optional<TunnelProcess::ClearTarget> TunnelProcess::nextToClear(CalculationContext& ctx) const {
    const BlockPos feet = baritone_.playerFeet();
    const DirOffset off = offsetOf(direction_);

    // The first non-passable block ahead, starting from the one in front.
    for (int step = 1; step <= 3; ++step) {
        ClearTarget target;
        target.feet = {feet.x + off.x * step, feet.y, feet.z + off.z * step};
        target.head = {feet.x + off.x * step, feet.y + 1, feet.z + off.z * step};

        const BlockProps& feetProps = ctx.props(target.feet.x, target.feet.y, target.feet.z);
        const BlockProps& headProps = ctx.props(target.head.x, target.head.y, target.head.z);
        const bool feetOpen = helper::canWalkThrough(ctx, target.feet.x, target.feet.y, target.feet.z);
        const bool headOpen = helper::canWalkThrough(ctx, target.head.x, target.head.y, target.head.z);
        if (feetOpen && headOpen) continue;
        // Do not tunnel into lava or water from above; let the path go around it.
        if (feetProps.type == BlockType::Lava || headProps.type == BlockType::Lava) return std::nullopt;
        return target;
    }
    return std::nullopt;
}

std::optional<PathingCommand> TunnelProcess::onTick(bool calcFailed, bool isSafeToCancel) {
    if (!active_) return std::nullopt;
    CalculationContext* live = baritone_.liveContext();
    if (!live) return std::nullopt;

    const BlockPos feet = baritone_.playerFeet();

    // Arrived? Length counts the blocks actually travelled, measured along the axis so
    // a detour does not end the tunnel early.
    if (length_ > 0 && alongAxis(start_, feet, direction_) >= length_) {
        baritone_.logDirect(std::format("Tunnel of {} blocks finished", length_), false);
        onLostControl();
        return std::nullopt;
    }

    // Stuck against something the path will not break: say so instead of pacing.
    if (feet == lastFeet_) {
        if (++stalled_ > 60) {
            baritone_.logDirect("Tunnel blocked; stopping", true);
            onLostControl();
            return std::nullopt;
        }
    } else {
        stalled_ = 0;
        lastFeet_ = feet;
    }

    // Aim the movement along the tunnel axis. GoalStrictDirection already does this,
    // but it only knows about the feet-level obstacle, so the head block is cleared
    // here by hand: a rotation at that block and a click, the same shape the core's
    // mining uses.
    if (auto target = nextToClear(*live)) {
        if (isSafeToCancel) {
            ExecContext ec{baritone_.player(), *live, baritone_.inventory(), feet};
            const BlockProps& headProps = ec.props(target->head);
            const BlockProps& feetProps = ec.props(target->feet);
            const BlockPos& block =
                ec.canWalkThrough(target->feet) ? target->head : target->feet;

            if (auto rot = movement_helper::reachable(ec, block)) {
                MovementState& controls = baritone_.controls();
                controls.setTarget({*rot, true});
                movement_helper::switchToBestToolFor(
                    ec, block == target->head ? headProps : feetProps);
                if (ec.isLookingAt(block) || ec.playerRotations().isReallyCloseTo(*rot)) {
                    controls.setInput(Input::ClickLeft, true);
                }
                return PathingCommand{nullptr, PathingCommandType::RequestPause};
            }
        }
    }

    if (!announced_) {
        announced_ = true;
        baritone_.logDirect(std::format("Tunnelling {} from {}", toName(direction_),
                                        feet),
                            false);
    }
    return PathingCommand{std::make_shared<GoalStrictDirection>(start_, direction_),
                          PathingCommandType::RevalidateGoalAndPath};
}

void TunnelProcess::onLostControl() {
    active_ = false;
    stalled_ = 0;
    announced_ = false;
}

std::string TunnelProcess::displayName() const {
    return std::format("Tunnel {}", toName(direction_));
}

double TunnelProcess::priority() const {
    // Below SurvivalProcess (which must be able to interrupt) and above the
    // goto/mine processes that default to the base priority.
    return 2.5;
}

}  // namespace baritone
