#pragma once
#include <string>

#include "baritone/process/Process.h"

namespace baritone {

// #tunnel <length> : a 1 wide, 2 high corridor straight ahead, at the player's feet
// level. Baritone's GoalStrictDirection does the travelling; this process exists to
// give it a fixed heading, a length, and a sensible stopping condition, and to keep
// the tunnel going when the corridor is interrupted (cave, water, a wandering mob).
//
// The corridor is 1x2, so both the feet-level block and the one above it have to go.
// Movement only ever aims at what is in front of the feet (positionsToBreak), so the
// head block is broken by this process rather than by the path: see onTick().
// Nearest of the four horizontal directions to a yaw (0 = north / -Z).
Direction nearestHorizontal(float yaw);

class TunnelProcess : public Process {
public:
    using Process::Process;

    // `direction` is snapped to the nearest of the four horizontal directions.
    // `length` <= 0 means "keep going until stopped".
    void start(Direction direction, int length);
    void cancel() { onLostControl(); }

    bool isActive() override { return active_; }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override;
    std::string displayName() const override;
    double priority() const override;

    Direction direction() const { return direction_; }
    int length() const { return length_; }

private:
    // The block the corridor needs cleared at this step: two high, starting at feet.
    struct ClearTarget {
        BlockPos feet;
        BlockPos head;
    };
    std::optional<ClearTarget> nextToClear(CalculationContext& ctx) const;

    bool active_ = false;
    Direction direction_ = Direction::North;
    int length_ = 0;
    BlockPos start_{};
    BlockPos lastFeet_{};
    int stalled_ = 0;
    bool announced_ = false;
};

}  // namespace baritone
