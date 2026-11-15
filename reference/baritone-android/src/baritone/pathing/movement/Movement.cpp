#include "baritone/pathing/movement/Movement.h"

#include <algorithm>
#include <limits>
#include <numbers>
#include <cmath>

#include "baritone/Settings.h"
#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/pathing/movement/MovementHelper.h"

namespace baritone {

using namespace costs;
using T = BlockType;
namespace ru = rotation_utils;

// ===========================================================================
// ExecContext
// ===========================================================================

BlockPos ExecContext::playerFeet() {
    Vec3d p = player.position();
    BlockPos feet{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y + 0.1251)),
                  static_cast<int>(std::floor(p.z))};
    // standing on top of a bottom slab counts as the block above
    if (props(feet).type == T::Slab && !props(feet).doubleSlab) return feet.above();
    return feet;
}

std::optional<BlockHit> ExecContext::objectMouseOver() {
    return rayTrace(bsi(), playerHead(), playerRotations(), player.blockReachDistance());
}

bool ExecContext::isLookingAt(const BlockPos& pos) {
    auto hit = objectMouseOver();
    return hit && hit->pos == pos;
}

bool ExecContext::canWalkThrough(const BlockPos& p) {
    return helper::canWalkThrough(calc, p.x, p.y, p.z);
}

bool ExecContext::canWalkOn(const BlockPos& p) {
    return helper::canWalkOn(calc, p.x, p.y, p.z);
}

// ===========================================================================
// MovementHelper (execution side)
// ===========================================================================

namespace movement_helper {

void moveTowards(ExecContext& ctx, MovementState& state, const BlockPos& pos) {
    Rotation r = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(pos), ctx.playerRotations())
                     .withPitch(ctx.playerRotations().pitch);
    state.setTarget({r, false}).setInput(Input::MoveForward, true);
}

void moveTowardsWithoutRotation(ExecContext& ctx, MovementState& state, float idealYaw) {
    constexpr float kDegToRad = 3.14159265358979f / 180.f;
    constexpr float kSprint = 1.3f;
    float yaw = ctx.playerRotations().yaw;
    float mx = std::sin(yaw * kDegToRad), mz = std::cos(yaw * kDegToRad);
    bool sprint = Settings::get().allowSprint;
    float fx = sprint ? mx * kSprint : mx, fz = sprint ? mz * kSprint : mz;
    struct Option {
        Input a;
        std::optional<Input> b;
        float x, z;
    };
    const Option options[] = {
        {Input::MoveForward, std::nullopt, fx, fz},
        {Input::MoveBack, std::nullopt, -mx, -mz},
        {Input::MoveLeft, std::nullopt, -mz, mx},
        {Input::MoveRight, std::nullopt, mz, -mx},
        {Input::MoveForward, Input::MoveLeft, fx - mz, fz + mx},
        {Input::MoveForward, Input::MoveRight, fx + mz, fz - mx},
        {Input::MoveBack, Input::MoveLeft, -mx - mz, -mz + mx},
        {Input::MoveBack, Input::MoveRight, -mx + mz, -mz - mx},
    };
    float ix = std::sin(idealYaw * kDegToRad), iz = std::cos(idealYaw * kDegToRad);
    const Option* best = nullptr;
    float bestDist = 0;
    for (const Option& o : options) {
        float d = std::fabs(o.x - ix) + std::fabs(o.z - iz);
        if (!best || d < bestDist) {
            best = &o;
            bestDist = d;
        }
    }
    state.setInput(best->a, true);
    if (best->b) state.setInput(*best->b, true);
}

void moveTowardsWithSlightRotation(ExecContext& ctx, MovementState& state, const BlockPos& dest) {
    float idealYaw = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(dest), ctx.playerRotations()).yaw;
    float yaw = ctx.playerRotations().yaw;
    float distance = std::fmod(Rotation::yawDistanceFromOffset(yaw, idealYaw), 45.f);
    float newYaw = distance > 0.f ? (distance > 22.5f ? distance - 45.f : distance)
                                  : (distance < -22.5f ? distance + 45.f : distance);
    state.setTarget({Rotation{yaw - newYaw, ctx.playerRotations().pitch}, true});
    moveTowardsWithoutRotation(ctx, state, idealYaw);
}

std::vector<BlockPos> steppingOnBlocks(ExecContext& ctx) {
    std::vector<BlockPos> out;
    Vec3d p = ctx.player.position();
    BlockPos at{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    constexpr double half = 0.3;
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) {
            double minX = at.x + x, minZ = at.z + z;
            if (p.x - half < minX + 1 && p.x + half > minX && p.z - half < minZ + 1 && p.z + half > minZ) {
                out.push_back({at.x + x, at.y - 1, at.z + z});
            }
        }
    return out;
}

bool steppingOnMagma(ExecContext& ctx) {
    if (!Settings::get().allowWalkOnMagmaBlocks) return false;
    for (const BlockPos& b : steppingOnBlocks(ctx))
        if (ctx.props(b).type == T::Magma) return true;
    return false;
}

namespace {

std::optional<Rotation> reachableOffset(ExecContext& ctx, const BlockPos& pos, const Vec3d& offsetPos, bool wouldSneak,
                                        double reach) {
    Vec3d eyes = ctx.playerHead();
    if (wouldSneak) eyes.y -= 0.3;  // TODO(bedrock): measure the sneaking eye height
    Rotation rotation = ru::calcRotationFromVec3d(eyes, offsetPos, ctx.playerRotations());
    auto hit = rayTrace(ctx.bsi(), eyes, rotation, reach < 0 ? ctx.player.blockReachDistance() : reach);
    if (hit) {
        if (hit->pos == pos) return rotation;
        if (ctx.props(pos).type == T::Fire && hit->pos == pos.below()) return rotation;
    }
    return std::nullopt;
}

}  // namespace

std::optional<Rotation> reachable(ExecContext& ctx, const BlockPos& pos, bool wouldSneak, double reach) {
    // Java remainWithExistingLookDirection: already looking at it, keep the look (not when
    // sneaking would move the eyes: then a rotation is computed from the sneaking eyes)
    if (Settings::get().remainWithExistingLookDirection && !wouldSneak && ctx.isLookingAt(pos)) {
        Rotation current = ctx.playerRotations();
        return Rotation{current.yaw, current.pitch + 0.0001f};
    }
    if (auto r = reachableOffset(ctx, pos, blockCenter(ctx.bsi(), pos), wouldSneak, reach)) return r;
    auto box = outlineBox(ctx.props(pos)).value_or(LocalBox{0, 0, 0, 1, 1, 1});
    static constexpr double sides[6][3] = {{0.5, 0, 0.5}, {0.5, 1, 0.5}, {0.5, 0.5, 0},
                                           {0.5, 0.5, 1}, {0, 0.5, 0.5}, {1, 0.5, 0.5}};
    for (const auto& s : sides) {
        double x = box.minX * s[0] + box.maxX * (1 - s[0]);
        double y = box.minY * s[1] + box.maxY * (1 - s[1]);
        double z = box.minZ * s[2] + box.maxZ * (1 - s[2]);
        if (auto r = reachableOffset(ctx, pos, {pos.x + x, pos.y + y, pos.z + z}, wouldSneak, reach)) return r;
    }
    return std::nullopt;
}

bool intersectsPlayer(const PlayerContext& player, const BlockPos& b) {
    constexpr double half = 0.3, height = 1.8;
    Vec3d p = player.position();
    return p.x - half < b.x + 1 && p.x + half > b.x && p.y < b.y + 1 && p.y + height > b.y && p.z - half < b.z + 1 &&
           p.z + half > b.z;
}

PlaceResult attemptToPlaceABlock(ExecContext& ctx, MovementState& state, const BlockPos& placeAt, bool preferDown,
                                 bool wouldSneak) {
    bool found = false;
    if (auto direct = reachable(ctx, placeAt, wouldSneak)) {
        state.setTarget({*direct, true});
        found = true;
    }
    for (Direction d : kPlaceAgainstDirs) {
        BlockPos against = placeAt.offset(d);
        if (!helper::canPlaceAgainst(ctx.props(against))) continue;
        if (!ctx.inventory.selectThrowawayForLocation(false, placeAt.x, placeAt.y, placeAt.z)) {
            state.setStatus(MovementStatus::Unreachable);  // no blocks to place
            return PlaceResult::NoOption;
        }
        Vec3d face{(placeAt.x + against.x + 1.0) * 0.5, (placeAt.y + against.y + 0.5) * 0.5,
                   (placeAt.z + against.z + 1.0) * 0.5};
        Vec3d eyes = ctx.playerHead();
        if (wouldSneak) eyes.y -= 0.3;
        Rotation place = ru::calcRotationFromVec3d(eyes, face, ctx.playerRotations());
        auto hit = rayTrace(ctx.bsi(), eyes, place, ctx.player.blockReachDistance());
        if (hit && hit->pos == against && hit->pos.offset(hit->face) == placeAt) {
            state.setTarget({place, true});
            found = true;
            if (!preferDown) break;  // preferDown: keep the last option (down)
        }
    }
    if (auto hit = ctx.objectMouseOver()) {
        if (hit->pos == placeAt ||
            (helper::canPlaceAgainst(ctx.props(hit->pos)) && hit->pos.offset(hit->face) == placeAt)) {
            if (wouldSneak) state.setInput(Input::Sneak, true);
            ctx.inventory.selectThrowawayForLocation(true, placeAt.x, placeAt.y, placeAt.z);
            return PlaceResult::ReadyToPlace;
        }
    }
    if (found) {
        if (wouldSneak) state.setInput(Input::Sneak, true);
        ctx.inventory.selectThrowawayForLocation(true, placeAt.x, placeAt.y, placeAt.z);
        return PlaceResult::Attempting;
    }
    return PlaceResult::NoOption;
}

void switchToBestToolFor(ExecContext& ctx, const BlockProps& block) {
    ctx.inventory.switchToBestToolFor(block);
}

}  // namespace movement_helper

namespace mh = movement_helper;

// ===========================================================================
// Chaining (lookahead into the next movement)
// ===========================================================================
// A movement used to report Success the instant it touched down, blind to what comes next:
// it either always killed the momentum (next jump short) or always kept it (next jump on a
// wrong trajectory). With a NextHint it can arrive in the state the next movement needs.
// The hint is ExecContext::next (Movement.h), rebuilt every tick by PathExecutor from the path:
// next = movements[pos + 1] of the current path, like the Java preTurnIntoTheBend reads
// positions[pos + 2]. Without it (ctx.next empty) everything falls back to the old behaviour.
namespace chain {

// Java-derived constants, same family as the in-flight predictor. Tune against Bedrock.
constexpr double kGravity = 0.08, kVDrag = 0.98, kHDrag = 0.91, kJumpVy = 0.42;
constexpr double kSprintBoost = 0.2, kGroundAccel = 0.1725, kAirAccel = 0.026;
constexpr double kGroundFriction = 0.546;  // 0.6 slipperiness * 0.91
constexpr double kMaxCarry = 0.20;         // best ground speed we can carry into a hop

// Blocks travelled along the jump axis from the jump tick until we are back down at dy above the
// takeoff height. v0 = horizontal speed along the axis in the tick before the jump.
// (v0 = 0.153, the steady sprint speed, gives ~3.9 flat: matches the usual 4-block sprint jump.)
inline double jumpTravel(double v0, double dy) {
    double v = v0 + kSprintBoost + kGroundAccel, vy = kJumpVy, y = kJumpVy, travel = v;
    vy = (vy - kGravity) * kVDrag;
    v *= kGroundFriction;  // the jump tick still uses ground friction
    for (int t = 1; t < 40; ++t) {
        v += kAirAccel;
        y += vy;
        travel += v;
        if (y <= dy && vy < 0) break;
        vy = (vy - kGravity) * kVDrag;
        v *= kHDrag;
    }
    return travel;
}

// Slowest takeoff speed that still covers `need` blocks (0: a standing jump is enough,
// infinity: not even a full run-up is).
inline double requiredSpeed(double need, double dy) {
    if (jumpTravel(0.0, dy) >= need) return 0.0;
    if (jumpTravel(kMaxCarry, dy) < need) return std::numeric_limits<double>::infinity();
    double lo = 0.0, hi = kMaxCarry;
    for (int i = 0; i < 24; ++i) {
        double mid = 0.5 * (lo + hi);
        (jumpTravel(mid, dy) >= need ? hi : lo) = mid;
    }
    return hi;
}

// Per-tick numbers of the ground run (same family as above). velocity() is the stored, post-friction
// value (~0.153 at a steady sprint); the distance actually covered in a tick is about v + kRunAccel
// (~0.28 at a steady sprint).
constexpr double kRunAccel = 0.13;      // sprinting ground acceleration added every tick
constexpr double kSteadySprint = 0.15;  // stored speed a run-up converges to (cannot build more)
constexpr double kSprintStep = 0.28;    // distance per tick at a steady sprint

// Where (along the jump axis, measured from the middle of the takeoff block) the Jump key has to be
// produced so that the jump TICK lands exactly on the block edge, never before it. Controls are
// applied one tick after they are produced, so lead the edge by what we will cover in that tick:
// the stored speed plus this tick's acceleration (~0.13 from standing, ~0.28 at full sprint).
// A fixed lead is only right at full speed; right after a landing we are slower and a fixed lead
// would start the hop well before the edge.
inline double jumpOutAt(double norm, double storedSpeed) {
    const double edge = 0.5 * norm;  // block boundary along the jump axis (corner on a diagonal)
    return edge - std::clamp(storedSpeed + kRunAccel, 0.0, kSprintStep);
}

// Distance covered by a run-up from a standstill until the stored speed reaches `goal`.
inline double runUpDistance(double goal) {
    double s = 0.0, dist = 0.0;
    for (int t = 0; t < 30 && s < goal - 0.01; ++t) {
        const double d = s + kRunAccel;
        dist += d;
        s = d * kGroundFriction;
    }
    return dist;
}

// Stored takeoff speed the jump src -> dest needs (see requiredSpeed). Shared by the landing logic
// (settle) and the takeoff, so both agree on what "fast enough" means.
inline double neededSpeed(int dist, double norm, int dy) {
    const double jumpAt = 0.5 * norm - kSprintStep;  // position before the jump tick at a full sprint
    const double need = dist * norm - 0.5 * norm - 0.3 - jumpAt + 0.25;  // +0.25 safety
    return requiredSpeed(need, dy);
}

// Same corner test as the Java preTurnIntoTheBend: straight chains share their heading exactly.
inline bool isCorner(int ax, int az, int bx, int bz) {
    return ax * bx + az * bz < 0.99 * std::hypot(ax, az) * std::hypot(bx, bz);
}

// Turn a wanted world-space push (px, pz) into movement keys for a player facing yawDeg
// (forward = (-sin yaw, cos yaw), left = (cos yaw, sin yaw)). Sets all four keys.
inline void pushKeys(MovementState& state, float yawDeg, double px, double pz) {
    const double r = yawDeg * std::numbers::pi / 180.0;
    const double fwd = px * -std::sin(r) + pz * std::cos(r);
    const double left = px * std::cos(r) + pz * std::sin(r);
    state.setInput(Input::MoveForward, fwd > 0.3);
    state.setInput(Input::MoveBack, fwd < -0.3);
    state.setInput(Input::MoveLeft, left > 0.3);
    state.setInput(Input::MoveRight, left < -0.3);
}

enum class Settle { Done, Settling, Impossible };

// Called every tick once we are over the landing block of a Fall or Parkour. Done = the body is in
// a state the next movement can start from (report Success). Settling = inputs were set, stay
// Running. Impossible = the next jump cannot be made from here (replan).
inline Settle settle(ExecContext& ctx, MovementState& state, const BlockPos& landing,
                     const std::optional<NextHint>& next, int& brakeTicks) {
    const Vec3d pos = ctx.player.position(), vel = ctx.player.velocity();
    const double speed = std::hypot(vel.x, vel.z);

    auto legacyBrake = [&]() {
        // no usable lookahead: bleed momentum so a narrow landing is not slid off
        // sneaking takes several ticks to bleed the speed: keep braking until we really are slow
        if (ctx.player.onGround() && speed > 0.06 && brakeTicks < 10) {
            ++brakeTicks;
            state.setInput(Input::Sneak, true).setInput(Input::Sprint, false);
            state.setInput(Input::MoveForward, false).setInput(Input::MoveBack, false);
            return Settle::Settling;
        }
        return Settle::Done;
    };

    if (!next || next->src != landing) return legacyBrake();

    const BlockPos d = next->dest - next->src;
    const int sx = (d.x > 0) - (d.x < 0), sz = (d.z > 0) - (d.z < 0);
    if (next->type != MovementType::Parkour) {
        // Walking on: keep a straight run going, stop for a turn.
        const BlockPos ahead = d;
        const double along = (vel.x * ahead.x + vel.z * ahead.z) / std::max(1.0, std::hypot(ahead.x, ahead.z));
        if (speed > 0.12 && along > 0.9 * speed) return Settle::Done;
        return legacyBrake();
    }

    const double norm = (sx != 0 && sz != 0) ? std::numbers::sqrt2 : 1.0;
    const int dist = std::max(std::abs(d.x), std::abs(d.z));
    const double along = (vel.x * sx + vel.z * sz) / norm;
    const double lateral = (vel.x * sz - vel.z * sx) / norm;  // sideways drift vs. the next axis
    const double out = ((pos.x - (landing.x + 0.5)) * sx + (pos.z - (landing.z + 0.5)) * sz) / norm;

    const double vReq = neededSpeed(dist, norm, d.y);
    const float yaw = static_cast<float>(std::atan2(-sx, sz) * 180.0 / std::numbers::pi);
    state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});

    const bool aligned = std::fabs(lateral) < 0.06;
    const bool fastEnough = along >= vReq - 0.01;

    if (aligned && fastEnough && out > -0.35) {
        // Chain: hand off with the momentum intact. Nothing set here survives (update() clears the
        // inputs of a finished movement, same as the Java clearAllKeys), so the hop is done by
        // the next Parkour's own jump logic, which presses Jump only when the jump tick will land
        // on the block edge (chain::jumpOutAt), never earlier.
        return Settle::Done;
    }

    if (!aligned) {
        // Momentum points off the next jump's line. Cancel it (sneak = edge-safe) and stay here
        // until we have REALLY stopped: it takes several ticks. Only then hand over; the next
        // Parkour builds its own run-up from the standstill (it is not instant either).
        if (speed < 0.03) return Settle::Done;
        state.setInput(Input::Sneak, true).setInput(Input::Sprint, false);
        state.setInput(Input::MoveForward, false).setInput(Input::MoveBack, false);
        return ++brakeTicks > 20 ? Settle::Impossible : Settle::Settling;
    }

    // Aligned but too slow for the next jump: needs a run-up on this block.
    const BlockPos back = landing.offset(-sx, 0, -sz);
    const bool runway = vReq <= 0.0 ||
                        (ctx.canWalkOn(back.below()) && ctx.canWalkThrough(back) && ctx.canWalkThrough(back.above()));
    if (!runway && speed < 0.03) return Settle::Impossible;
    // Nothing to press here: the inputs of a finished movement are cleared anyway, so "sprint now"
    // set at this point never built any speed. The next Parkour walks back and runs up for real
    // (MovementParkour::runPhase_); if there is no runway it is best effort and re-checks.
    return Settle::Done;
}

}  // namespace chain

// ===========================================================================
// Movement base
// ===========================================================================

Movement::Movement(MovementType type, BlockPos src, BlockPos dest, std::vector<BlockPos> toBreak,
                   std::optional<BlockPos> toPlace)
    : type_(type), src_(src), dest_(dest), positionsToBreak_(std::move(toBreak)), positionToPlace_(toPlace) {}

double Movement::calculateCost(CalculationContext& ctx) const {
    MoveResult res;
    switch (type_) {
        case MovementType::Traverse: return movement_costs::traverse(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z);
        case MovementType::Ascend: return movement_costs::ascend(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z);
        case MovementType::Descend:
        case MovementType::Fall:
            movement_costs::descend(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z, res);
            return (res.x == dest_.x && res.y == dest_.y && res.z == dest_.z) ? res.cost : COST_INF;
        case MovementType::Diagonal:
            movement_costs::diagonal(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z, res);
            return res.y == dest_.y ? res.cost : COST_INF;
        case MovementType::Pillar: return movement_costs::pillar(ctx, src_.x, src_.y, src_.z);
        case MovementType::Downward: return movement_costs::downward(ctx, src_.x, src_.y, src_.z);
        case MovementType::Parkour: {
            BlockPos d = direction();
            if (d.x != 0 && d.z != 0) {
                movement_costs::parkourDiagonal(ctx, src_.x, src_.y, src_.z, d.x > 0 ? 1 : -1, d.z > 0 ? 1 : -1, res);
                return (res.x == dest_.x && res.y == dest_.y && res.z == dest_.z) ? res.cost : COST_INF;
            }
            Direction dir = d.x > 0 ? Direction::East : d.x < 0 ? Direction::West : d.z > 0 ? Direction::South
                                                                                              : Direction::North;
            movement_costs::parkour(ctx, src_.x, src_.y, src_.z, dir, res);
            return (res.x == dest_.x && res.y == dest_.y && res.z == dest_.z) ? res.cost : COST_INF;
        }
    }
    return COST_INF;
}

const std::unordered_set<BlockPos>& Movement::validPositions() {
    if (!validPositions_) validPositions_ = calculateValidPositions();
    return *validPositions_;
}

bool Movement::playerInValidPosition(ExecContext& ctx) {
    return validPositions().contains(ctx.playerFeet()) || validPositions().contains(ctx.pathStart);
}

void Movement::reset() {
    currentState_ = MovementState{};
}

std::vector<BlockPos> Movement::toBreak(CalculationContext& c) {
    std::vector<BlockPos> result;
    for (const BlockPos& p : positionsToBreak_)
        if (!helper::canWalkThrough(c, p.x, p.y, p.z)) result.push_back(p);
    return result;
}

std::vector<BlockPos> Movement::toPlace(CalculationContext& c) {
    std::vector<BlockPos> result;
    if (positionToPlace_ && !helper::canWalkOn(c, positionToPlace_->x, positionToPlace_->y, positionToPlace_->z)) {
        result.push_back(*positionToPlace_);
    }
    return result;
}

MovementStatus Movement::update(ExecContext& ctx) {
    MovementState state = currentState_;
    state.inputs.reset();
    state.target = {};
    updateState(ctx, state);
    if (ctx.isLiquid(ctx.playerFeet()) && ctx.player.position().y < dest_.y + 0.6) {
        state.setInput(Input::Jump, true);  // swim up
    }
    currentState_ = state;
    lastOutput_ = state;
    if (isComplete(state.status)) lastOutput_.inputs.reset();
    return state.status;
}

bool Movement::prepared(ExecContext& ctx, MovementState& state) {
    if (state.status == MovementStatus::Waiting) return true;
    bool somethingInTheWay = false;
    // Java pauseMiningForFallingBlocks: wait while a falling block (sand, gravel) is coming down
    // into a block to break
    std::vector<Vec3d> falling;
    if (Settings::get().pauseMiningForFallingBlocks) {
        for (const EntityInfo& e : ctx.player.entities())
            if (e.type == "minecraft:falling_block") falling.push_back(e.position);
    }
    for (const BlockPos& pos : positionsToBreak_) {
        for (const Vec3d& f : falling) {
            // the entity's 0.98 box against (0, 0, 0, 1, 1.1, 1) at pos
            if (f.x + 0.49 > pos.x && f.x - 0.49 < pos.x + 1 && f.z + 0.49 > pos.z && f.z - 0.49 < pos.z + 1 &&
                f.y + 0.98 > pos.y && f.y < pos.y + 1.1)
                return false;
        }
        if (!ctx.canWalkThrough(pos)) {
            if (ctx.calc.assumeAllBlocksUnmineable) {
                state.setStatus(MovementStatus::Unreachable);
                return true;
            }
            somethingInTheWay = true;
            mh::switchToBestToolFor(ctx, ctx.props(pos));
            if (auto reach = mh::reachable(ctx, pos)) {
                state.setTarget({*reach, true});
                if (ctx.isLookingAt(pos) || ctx.playerRotations().isReallyCloseTo(*reach)) {
                    state.setInput(Input::ClickLeft, true);
                }
                return false;
            }
            // can't see any face: break whatever is in the way towards the center
            state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(pos), ctx.playerRotations()), true});
            state.setInput(Input::ClickLeft, true);
            return false;
        }
    }
    if (somethingInTheWay) {
        state.setStatus(MovementStatus::Unreachable);
        return true;
    }
    return true;
}

MovementState& Movement::updateState(ExecContext& ctx, MovementState& state) {
    if (!prepared(ctx, state)) return state.setStatus(MovementStatus::Prepping);
    if (state.status == MovementStatus::Prepping) state.setStatus(MovementStatus::Waiting);
    if (state.status == MovementStatus::Waiting) state.setStatus(MovementStatus::Running);
    return state;
}

// ===========================================================================
// Traverse
// ===========================================================================

class MovementTraverse : public Movement {
public:
    MovementTraverse(BlockPos src, BlockPos dest)
        : Movement(MovementType::Traverse, src, dest, {dest.above(), dest}, dest.below()) {}

    bool safeToCancel(ExecContext& ctx) const override {
        // safe while breaking blocks or when the bridge block is already there
        return currentState_.status != MovementStatus::Running || ctx.canWalkOn(dest_.below());
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_}; }

    bool prepared(ExecContext& ctx, MovementState& state) override {
        BlockPos feet = ctx.playerFeet();
        if (feet == src_ || feet == src_.below()) {
            if (helper::isClimbable(ctx.props(src_.below()))) state.setInput(Input::Sneak, true);
        }
        return Movement::prepared(ctx, state);
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        const BlockProps& pb0 = ctx.props(positionsToBreak_[0]);
        const BlockProps& pb1 = ctx.props(positionsToBreak_[1]);
        Vec3d pos = ctx.player.position();
        if (state.status != MovementStatus::Running) {
            // walk while breaking the blocks in front
            if (!Settings::get().walkWhileBreaking || state.status != MovementStatus::Prepping) return state;
            if (helper::avoidWalkingInto(pb0) || helper::avoidWalkingInto(pb1)) return state;
            double dist = std::max(std::fabs(pos.x - (dest_.x + 0.5)), std::fabs(pos.z - (dest_.z + 0.5)));
            if (dist < 0.83) return state;
            if (!state.target.rotation) return state;
            float yawToDest = ru::calcRotationFromVec3d(ctx.playerHead(), blockCenter(ctx.bsi(), dest_), ctx.playerRotations()).yaw;
            float pitchToBreak = state.target.rotation->pitch;
            if (helper::isBlockNormalCube(pb0) || (pb0.type == T::Air && (helper::isBlockNormalCube(pb1) || pb1.type == T::Air))) {
                pitchToBreak = 26;  // break efficiently at this angle until we're against the block
            }
            return state.setTarget({Rotation{yawToDest, pitchToBreak}, true})
                .setInput(Input::MoveForward, true)
                .setInput(Input::Sprint, true);
        }

        const BlockProps& fd = ctx.props(src_.below());
        bool ladder = helper::isClimbable(fd);

        state.setInput(Input::Sneak, mh::steppingOnMagma(ctx));

        // closed doors / gates: open them
        if ((pb1.type == T::Door && !pb1.open) || (pb0.type == T::Door && !pb0.open)) {
            BlockPos door = pb1.type == T::Door ? positionsToBreak_[1] : positionsToBreak_[0];
            return state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), blockCenter(ctx.bsi(), door), ctx.playerRotations()), true})
                .setInput(Input::ClickRight, true);
        }
        if ((pb1.type == T::FenceGate && !pb1.open) || (pb0.type == T::FenceGate && !pb0.open)) {
            BlockPos gate = pb1.type == T::FenceGate ? positionsToBreak_[1] : positionsToBreak_[0];
            if (auto r = mh::reachable(ctx, gate)) return state.setTarget({*r, true}).setInput(Input::ClickRight, true);
        }

        bool isTheBridgeBlockThere = ctx.canWalkOn(*positionToPlace_) || ladder ||
                                     helper::canUseFrostWalker(ctx.calc, ctx.props(*positionToPlace_));
        BlockPos feet = ctx.playerFeet();
        if (feet.y != dest_.y && !ladder) {
            if (feet.y < dest_.y) return state.setInput(Input::Jump, true);
            return state;
        }

        if (isTheBridgeBlockThere) {
            if (feet == dest_) return state.setStatus(MovementStatus::Success);
            BlockPos dir = direction();
            if (Settings::get().overshootTraverse && (feet == dest_ + dir || feet == dest_ + dir + dir)) {
                return state.setStatus(MovementStatus::Success);
            }
            const BlockProps& low = ctx.props(src_);
            const BlockProps& high = ctx.props(src_.above());
            if (pos.y > src_.y + 0.1 && !ctx.player.onGround() && (helper::isClimbable(low) || helper::isClimbable(high))) {
                return state;  // pressing forward could climb the ladder instead
            }
            BlockPos into = dest_ + dir;
            const BlockProps& intoBelow = ctx.props(into);
            const BlockProps& intoAbove = ctx.props(into.above());
            if (wasTheBridgeBlockAlwaysThere_ && (!ctx.isLiquid(feet) || Settings::get().sprintInWater) &&
                (!helper::avoidWalkingInto(intoBelow) || helper::isWater(intoBelow)) && !helper::avoidWalkingInto(intoAbove)) {
                state.setInput(Input::Sprint, true);
            }
            const BlockProps& destDown = ctx.props(dest_.below());
            if (feet.y != dest_.y && ladder && helper::isClimbable(destDown)) state.setInput(Input::Jump, true);
            mh::moveTowards(ctx, state, positionsToBreak_[0]);
            return state;
        }

        // bridging: place a block under the destination
        wasTheBridgeBlockAlwaysThere_ = false;
        const BlockProps& standingOn = ctx.props(feet.below());
        if (standingOn.type == T::SoulSand || standingOn.type == T::Slab) {
            double dist = std::max(std::fabs(dest_.x + 0.5 - pos.x), std::fabs(dest_.z + 0.5 - pos.z));
            if (dist < 0.85) {
                mh::moveTowards(ctx, state, dest_);
                return state.setInput(Input::MoveForward, false).setInput(Input::MoveBack, true);
            }
        }
        double dist1 = std::max(std::fabs(pos.x - (dest_.x + 0.5)), std::fabs(pos.z - (dest_.z + 0.5)));
        bool safeWalk = Settings::get().assumeSafeWalk;
        auto p = mh::attemptToPlaceABlock(ctx, state, dest_.below(), false, !safeWalk);
        if ((p == mh::PlaceResult::ReadyToPlace || dist1 < 0.6) && !safeWalk) state.setInput(Input::Sneak, true);
        switch (p) {
            case mh::PlaceResult::ReadyToPlace:
                if (ctx.player.isSneaking() || safeWalk) state.setInput(Input::ClickRight, true);
                return state;
            case mh::PlaceResult::Attempting:
                if (dist1 > 0.83) {
                    float yaw = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(dest_), ctx.playerRotations()).yaw;
                    if (state.target.rotation && std::fabs(state.target.rotation->yaw - yaw) < 0.1f) {
                        return state.setInput(Input::MoveForward, true);
                    }
                }
                return state;
            default: break;
        }
        if (feet == dest_) {
            // sneaking over air: place against the block we just walked off of
            double faceX = (dest_.x + src_.x + 1.0) * 0.5;
            double faceY = (dest_.y + src_.y - 1.0) * 0.5;
            double faceZ = (dest_.z + src_.z + 1.0) * 0.5;
            BlockPos goalLook = src_.below();
            Rotation backToFace = ru::calcRotationFromVec3d(ctx.playerHead(), {faceX, faceY, faceZ}, ctx.playerRotations());
            double dist2 = std::max(std::fabs(pos.x - faceX), std::fabs(pos.z - faceZ));
            if (dist2 < 0.29) {
                float yaw = ru::calcRotationFromVec3d(Vec3d::center(dest_), ctx.playerHead(), ctx.playerRotations()).yaw;
                state.setTarget({Rotation{yaw, backToFace.pitch}, true});
                state.setInput(Input::MoveBack, true);
            } else {
                state.setTarget({backToFace, true});
            }
            if (ctx.isLookingAt(goalLook)) return state.setInput(Input::ClickRight, true);
            return state;
        }
        mh::moveTowardsWithSlightRotation(ctx, state, dest_);
        return state;
    }

private:
    bool wasTheBridgeBlockAlwaysThere_ = true;
};

// ===========================================================================
// Ascend
// ===========================================================================

class MovementAscend : public Movement {
public:
    MovementAscend(BlockPos src, BlockPos dest)
        : Movement(MovementType::Ascend, src, dest, {src.above(2), dest, dest.above()}, dest.below()) {}

    bool safeToCancel(ExecContext&) const override {
        return currentState_.status != MovementStatus::Running || ticksWithoutPlacement_ == 0;
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        BlockPos prior = (src_ - direction()).above();  // backing up to place, sprint ascends
        return {src_, src_.above(), dest_, prior, prior.above()};
    }

    // Java headBonkClear: nothing around the head to bonk on, so jumping right away is safe
    bool headBonkClear(ExecContext& ctx) {
        BlockPos startUp = src_.above(2);
        for (Direction d : kHorizontals)
            if (!ctx.canWalkThrough(startUp.offset(d))) return false;
        return true;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        if (ctx.playerFeet().y < src_.y) return state.setStatus(MovementStatus::Unreachable);
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;

        BlockPos feet = ctx.playerFeet();
        if (feet == dest_ || feet == dest_ + direction().below()) return state.setStatus(MovementStatus::Success);

        const BlockProps& jumpingOnto = ctx.props(*positionToPlace_);
        if (!ctx.canWalkOn(*positionToPlace_)) {
            ticksWithoutPlacement_++;
            if (mh::attemptToPlaceABlock(ctx, state, dest_.below(), false, true) == mh::PlaceResult::ReadyToPlace) {
                state.setInput(Input::Sneak, true);
                if (ctx.player.isSneaking()) state.setInput(Input::ClickRight, true);
            }
            if (ticksWithoutPlacement_ > 10) state.setInput(Input::MoveBack, true);  // we may be in the way
            return state;
        }
        mh::moveTowards(ctx, state, dest_);
        state.setInput(Input::Sneak, Settings::get().allowWalkOnMagmaBlocks && jumpingOnto.type == T::Magma);
        if (helper::isBottomSlab(jumpingOnto) && !helper::isBottomSlab(ctx.props(src_.below()))) {
            return state;  // walk onto the slab, don't jump
        }
        if (Settings::get().assumeStep || feet == src_.above()) return state;  // step up / already jumping

        int xAxis = std::abs(src_.x - dest_.x), zAxis = std::abs(src_.z - dest_.z);
        Vec3d pos = ctx.player.position();
        Vec3d vel = ctx.player.velocity();
        double flatDistToNext = xAxis * std::fabs(dest_.x + 0.5 - pos.x) + zAxis * std::fabs(dest_.z + 0.5 - pos.z);
        double sideDist = zAxis * std::fabs(dest_.x + 0.5 - pos.x) + xAxis * std::fabs(dest_.z + 0.5 - pos.z);
        double lateralMotion = xAxis * vel.z + zAxis * vel.x;
        if (std::fabs(lateralMotion) > 0.1) return state;
        if (headBonkClear(ctx)) return state.setInput(Input::Jump, true);
        if (flatDistToNext > 1.2 || sideDist > 0.2) return state;
        return state.setInput(Input::Jump, true);
    }

private:
    int ticksWithoutPlacement_ = 0;
};

// ===========================================================================
// Descend
// ===========================================================================

class MovementDescend : public Movement {
public:
    MovementDescend(BlockPos src, BlockPos dest)
        : Movement(MovementType::Descend, src, dest, {dest.above(2), dest.above(), dest}, dest.below()) {}

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_.above(), dest_}; }

    bool skipToAscend(ExecContext& ctx) {
        BlockPos into = dest_ - src_.below() + dest_;
        return !ctx.canWalkThrough(into) && ctx.canWalkThrough(into.above()) && ctx.canWalkThrough(into.above(2));
    }

    bool descendSafeMode(ExecContext& ctx) override { return safeMode(ctx); }
    bool descendSkipToAscend(ExecContext& ctx) override { return skipToAscend(ctx); }
    void forceSafeMode() override { forceSafeMode_ = true; }

    bool safeMode(ExecContext& ctx) {
        if (forceSafeMode_) return true;
        if (skipToAscend(ctx)) return true;
        BlockPos into = dest_ - src_.below() + dest_;
        for (int y = 0; y <= 2; ++y)
            if (helper::avoidWalkingInto(ctx.props(into.above(y)))) return true;
        return false;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;

        BlockPos feet = ctx.playerFeet();
        BlockPos fakeDest{dest_.x * 2 - src_.x, dest_.y, dest_.z * 2 - src_.z};
        Vec3d pos = ctx.player.position();
        if ((feet == dest_ || feet == fakeDest) && (ctx.isLiquid(dest_) || pos.y - dest_.y < 0.5)) {
            return state.setStatus(MovementStatus::Success);
        }
        Rotation cur = ctx.playerRotations();
        if (safeMode(ctx)) {
            double destX = (src_.x + 0.5) * 0.17 + (dest_.x + 0.5) * 0.83;
            double destZ = (src_.z + 0.5) * 0.17 + (dest_.z + 0.5) * 0.83;
            float yaw = ru::calcRotationFromVec3d(ctx.playerHead(), {destX, static_cast<double>(dest_.y), destZ}, cur).yaw;
            return state.setTarget({Rotation{yaw, cur.pitch}, false}).setInput(Input::MoveForward, true);
        }
        double diffX = pos.x - (dest_.x + 0.5), diffZ = pos.z - (dest_.z + 0.5);
        double ab = std::sqrt(diffX * diffX + diffZ * diffZ);
        double x = pos.x - (src_.x + 0.5), z = pos.z - (src_.z + 0.5);
        double fromStart = std::sqrt(x * x + z * z);
        {
            Vec3d p = ctx.player.position();
            BlockPos under{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)) - 1,
                           static_cast<int>(std::floor(p.z))};
            state.setInput(Input::Sneak, Settings::get().allowWalkOnMagmaBlocks && ctx.props(under).type == T::Magma);
        }
        if (feet != dest_ || ab > 0.25) {
            if (numTicks_++ < 20 && fromStart < 1.25) mh::moveTowards(ctx, state, fakeDest);
            else mh::moveTowards(ctx, state, dest_);
        }
        return state;
    }

private:
    int numTicks_ = 0;
    bool forceSafeMode_ = false;
};

// ===========================================================================
// Fall
// ===========================================================================

class MovementFall : public Movement {
public:
    MovementFall(BlockPos src, BlockPos dest) : Movement(MovementType::Fall, src, dest, buildBreak(src, dest), std::nullopt) {}

    bool safeToCancel(ExecContext& ctx) const override {
        return ctx.playerFeet() == src_ || currentState_.status != MovementStatus::Running;
    }

protected:
    static std::vector<BlockPos> buildBreak(BlockPos src, BlockPos dest) {
        int diffX = src.x - dest.x, diffZ = src.z - dest.z, diffY = std::abs(src.y - dest.y);
        std::vector<BlockPos> out;
        for (int i = 0; i < diffY + 2; ++i) out.push_back({src.x - diffX, src.y + 1 - i, src.z - diffZ});
        return out;
    }

    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> set{src_};
        for (int y = src_.y - dest_.y; y >= 0; --y) set.insert(dest_.above(y));
        return set;
    }

    void reset() override {
        Movement::reset();
        bucketUsed_ = false;
        landBrakeTicks_ = 0;
    }

    bool bucketUsed_ = false;  // the water bucket was emptied during this fall
    int landBrakeTicks_ = 0;

    // only the first blocks matter before jumping off (Java MovementFall.prepared)
    bool prepared(ExecContext& ctx, MovementState& state) override {
        if (state.status == MovementStatus::Waiting) return true;
        for (std::size_t i = 0; i < 4 && i < positionsToBreak_.size(); ++i) {
            if (!ctx.canWalkThrough(positionsToBreak_[i])) return Movement::prepared(ctx, state);
        }
        return true;
    }

    // facing of the first ladder below us: stay against it while falling
    std::optional<BlockPos> avoid(ExecContext& ctx) {
        BlockPos feet = ctx.playerFeet();
        for (int i = 0; i < 15; ++i) {
            const BlockProps& p = ctx.props(feet.below(i));
            if (p.type != T::Ladder) continue;
            std::string_view dir = p.state("facing_direction");  // 2 north, 3 south, 4 west, 5 east
            if (dir == "2") return BlockPos{0, 0, -1};
            if (dir == "3") return BlockPos{0, 0, 1};
            if (dir == "4") return BlockPos{-1, 0, 0};
            if (dir == "5") return BlockPos{1, 0, 0};
        }
        return std::nullopt;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;

        BlockPos feet = ctx.playerFeet();
        Vec3d pos = ctx.player.position();
        Vec3d vel = ctx.player.velocity();
        Rotation toDest = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(dest_), ctx.playerRotations());
        std::optional<Rotation> targetRotation;
        bool isWater = helper::isWater(ctx.props(dest_));
        if (ctx.props(dest_.below()).type == T::Magma) {
            bool allAir = true;
            for (const BlockPos& b : mh::steppingOnBlocks(ctx)) allAir = allAir && ctx.canWalkThrough(b);
            if (allAir) state.setInput(Input::Sneak, true);
        }
        bool willPlaceBucket = src_.y - dest_.y > Settings::get().maxFallHeightNoWater + 1;  // see the cost
        if (!isWater && willPlaceBucket && feet != dest_) {
            int bucket = ctx.inventory.findSlot("minecraft:water_bucket");
            if (bucket < 0 && !ctx.player.onGround() && ctx.player.hotbar(ctx.player.selectedSlot()).name == "minecraft:bucket") {
                // just emptied: the water is placed but not read back yet (Java sees it at once)
                ctx.player.refreshWorld(dest_, 1);
                bucket = ctx.player.selectedSlot();
                bucketUsed_ = true;
            }
            if (bucket < 0 || ctx.calc.bsi.world().dimension() == 1) return state.setStatus(MovementStatus::Unreachable);
            // Bedrock: the click waits a few ticks after a slot change (the server must know the
            // held item) and the camera must be down when it comes: hold the bucket and look down
            // as soon as the fall starts, not only within reach like Java
            if (!ctx.player.onGround() && pos.y < src_.y - 0.5) {
                ctx.player.selectSlot(bucket);
                targetRotation = Rotation{toDest.yaw, 90.f};
                // Java clicks once within reach; falling ~4 blocks per tick, the reach can be
                // crossed between two ticks, so the click is armed every frame from 2 ticks before
                double nextFall = std::fabs((vel.y - 0.08) * 0.98);
                if (!bucketUsed_ && ctx.player.selectedSlot() == bucket && ctx.playerRotations().pitch > 75 &&
                    pos.y - dest_.y < ctx.player.blockReachDistance() + 2 * nextFall + 2)
                    ctx.player.useHeldItemWhenTargeting();
            }
        }
        state.setTarget(targetRotation ? MovementTarget{targetRotation, true} : MovementTarget{toDest, false});
        // Bedrock: a player floating in water has its feet about a block lower than in Java
        bool floatingInDest = isWater && feet == dest_.below() && helper::isWater(ctx.props(feet));
        if ((feet == dest_ || floatingInDest) && (pos.y - feet.y < 0.094 || isWater)) {
            if (isWater) {
                int empty = ctx.inventory.findSlot("minecraft:bucket");
                if (empty >= 0 && willPlaceBucket) {
                    ctx.player.selectSlot(empty);
                    if (vel.y >= 0) {
                        return state.setTarget({Rotation{toDest.yaw, 90.f}, true}).setInput(Input::ClickRight, true);
                    }
                    return state;
                }
                if (vel.y >= 0) return state.setStatus(MovementStatus::Success);
            } else {
                switch (chain::settle(ctx, state, dest_, ctx.next, landBrakeTicks_)) {
                    case chain::Settle::Done: return state.setStatus(MovementStatus::Success);
                    case chain::Settle::Settling: return state;
                    case chain::Settle::Impossible: return state.setStatus(MovementStatus::Unreachable);
                }
            }
        }
        Vec3d destCenter = Vec3d::center(dest_);
        if (std::fabs(pos.x + vel.x - destCenter.x) > 0.1 || std::fabs(pos.z + vel.z - destCenter.z) > 0.1) {
            if (!ctx.player.onGround() && std::fabs(vel.y) > 0.4) state.setInput(Input::Sneak, true);
            state.setInput(Input::MoveForward, true);
        }
        BlockPos avoidDir = src_ - dest_;
        if (auto ladder = avoid(ctx)) {
            avoidDir = *ladder;
            double dist = std::fabs(avoidDir.x * (destCenter.x - avoidDir.x / 2.0 - pos.x)) +
                          std::fabs(avoidDir.z * (destCenter.z - avoidDir.z / 2.0 - pos.z));
            if (dist < 0.6) state.setInput(Input::MoveForward, true);
            else if (!ctx.player.onGround()) state.setInput(Input::Sneak, false);
        }
        if (!targetRotation) {
            Vec3d offset{destCenter.x + 0.125 * avoidDir.x, destCenter.y, destCenter.z + 0.125 * avoidDir.z};
            state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), offset, ctx.playerRotations()), false});
        }
        return state;
    }
};

// ===========================================================================
// Diagonal
// ===========================================================================

class MovementDiagonal : public Movement {
public:
    MovementDiagonal(BlockPos src, BlockPos dest)
        : Movement(MovementType::Diagonal, src, dest,
                   {{src.x, src.y, dest.z}, {src.x, src.y + 1, dest.z}, {dest.x, src.y, src.z}, {dest.x, src.y + 1, src.z},
                    dest, dest.above()},
                   std::nullopt) {}

    bool safeToCancel(ExecContext& ctx) const override {
        // Java MovementDiagonal.safeToCancel: not while cutting a corner over nothing
        BlockPos feet = ctx.playerFeet();
        if (feet == src_) return true;
        if (ctx.canWalkOn({src_.x, src_.y - 1, dest_.z}) && ctx.canWalkOn({dest_.x, src_.y - 1, src_.z})) return true;
        if (feet == BlockPos{src_.x, src_.y, dest_.z} || feet == BlockPos{dest_.x, src_.y, src_.z}) {
            Vec3d p = ctx.player.position();
            double y = p.y - 1, o = 0.25;
            auto on = [&](double x, double z) {
                return ctx.canWalkOn({static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
                                      static_cast<int>(std::floor(z))});
            };
            return on(p.x + o, p.z + o) || on(p.x + o, p.z - o) || on(p.x - o, p.z + o) || on(p.x - o, p.z - o);
        }
        return true;
    }

    std::vector<BlockPos> toWalkInto(CalculationContext& c) override {
        std::vector<BlockPos> out;
        for (int i = 0; i < 4; ++i)
            if (!helper::canWalkThrough(c, positionsToBreak_[i].x, positionsToBreak_[i].y, positionsToBreak_[i].z)) {
                out.push_back(positionsToBreak_[i]);
            }
        return out;
    }

protected:
    bool prepared(ExecContext&, MovementState&) override { return true; }

    std::unordered_set<BlockPos> calculateValidPositions() const override {
        BlockPos diagA{src_.x, src_.y, dest_.z}, diagB{dest_.x, src_.y, src_.z};
        if (dest_.y < src_.y) return {src_, dest_.above(), diagA, diagB, dest_, diagA.below(), diagB.below()};
        if (dest_.y > src_.y) return {src_, src_.above(), diagA, diagB, dest_, diagA.above(), diagB.above()};
        return {src_, dest_, diagA, diagB};
    }

    bool sprint(ExecContext& ctx) {
        if (ctx.isLiquid(ctx.playerFeet()) && !Settings::get().sprintInWater) return false;
        for (int i = 0; i < 4; ++i)
            if (!ctx.canWalkThrough(positionsToBreak_[i])) return false;
        return true;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        BlockPos feet = ctx.playerFeet();
        if (feet == dest_) {
            // Sprint-jumping onto a block leaves horizontal momentum that can carry the player
            // off a narrow landing. Sneak for one tick to bleed it off before moving on.
            const Vec3d vel = ctx.player.velocity();
            if (ctx.player.onGround() && std::hypot(vel.x, vel.z) > 0.24 && landBrakeTicks_ < 1) {
                ++landBrakeTicks_;
                state.setInput(Input::Sneak, true);
                state.setInput(Input::Sprint, false);
                state.setInput(Input::MoveForward, false);
                return state;
            }
            return state.setStatus(MovementStatus::Success);
        }
        if (!playerInValidPosition(ctx) && !(ctx.isLiquid(src_) && validPositions().contains(feet.above()))) {
            return state.setStatus(MovementStatus::Unreachable);
        }
        // TODO(bedrock): horizontalCollision -> jump for diagonal ascends
        if (dest_.y > src_.y && ctx.player.position().y < src_.y + 0.1) state.setInput(Input::Jump, true);
        if (sprint(ctx)) state.setInput(Input::Sprint, true);
        state.setInput(Input::Sneak, mh::steppingOnMagma(ctx));
        mh::moveTowards(ctx, state, dest_);
        return state;
    }

private:
    int ticksWithoutPlacement_ = 0;
    int landBrakeTicks_ = 0;
};

// ===========================================================================
// Pillar
// ===========================================================================

class MovementPillar : public Movement {
public:
    MovementPillar(BlockPos src, BlockPos dest) : Movement(MovementType::Pillar, src, dest, {src.above(2)}, src) {}

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_}; }

    bool prepared(ExecContext& ctx, MovementState& state) override {
        BlockPos feet = ctx.playerFeet();
        if ((feet == src_ || feet == src_.below()) && helper::isClimbable(ctx.props(src_.below()))) {
            state.setInput(Input::Sneak, true);
        }
        if (helper::isWater(ctx.props(dest_.above()))) return true;
        return Movement::prepared(ctx, state);
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        BlockPos feet = ctx.playerFeet();
        if (feet.y < src_.y) return state.setStatus(MovementStatus::Unreachable);

        const BlockProps& fromDown = ctx.props(src_);
        Vec3d pos = ctx.player.position();
        if (helper::isWater(fromDown) && helper::isWater(ctx.props(dest_))) {
            state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(dest_), ctx.playerRotations()), false});
            Vec3d c = Vec3d::center(dest_);
            if (std::fabs(pos.x - c.x) > 0.2 || std::fabs(pos.z - c.z) > 0.2) state.setInput(Input::MoveForward, true);
            if (feet == dest_) return state.setStatus(MovementStatus::Success);
            return state;
        }
        bool ladder = helper::isClimbable(fromDown);
        Rotation rotation = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(*positionToPlace_), ctx.playerRotations());
        if (!ladder) state.setTarget({ctx.playerRotations().withPitch(rotation.pitch), true});

        bool blockIsThere = ctx.canWalkOn(src_) || ladder;
        if (ladder) {
            if (feet == dest_) return state.setStatus(MovementStatus::Success);
            mh::moveTowards(ctx, state, dest_);
            state.setInput(Input::Jump, true);
            return state;
        }

        if (!ctx.inventory.selectThrowawayForLocation(true, src_.x, src_.y, src_.z)) {
            return state.setStatus(MovementStatus::Unreachable);
        }
        state.setInput(Input::Sneak, true);
        double diffX = pos.x - (dest_.x + 0.5), diffZ = pos.z - (dest_.z + 0.5);
        double dist = std::sqrt(diffX * diffX + diffZ * diffZ);
        Vec3d vel = ctx.player.velocity();
        double flatMotion = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        if (dist > 0.17) {
            state.setInput(Input::MoveForward, true);
            state.setTarget({rotation, true});
        } else if (flatMotion < 0.05) {
            state.setInput(Input::Jump, pos.y < dest_.y);
        }
        if (!blockIsThere) {
            const BlockProps& fr = ctx.props(src_);
            if (!(fr.type == T::Air || fr.replaceable)) {
                return state.setStatus(MovementStatus::Unreachable);
            } else if (ctx.player.isSneaking() && (ctx.isLookingAt(src_.below()) || ctx.isLookingAt(src_)) &&
                       pos.y > dest_.y + 0.1) {
                state.setInput(Input::ClickRight, true);
            }
        }
        if (feet == dest_ && blockIsThere) return state.setStatus(MovementStatus::Success);
        return state;
    }
};

// ===========================================================================
// Downward
// ===========================================================================

class MovementDownward : public Movement {
public:
    MovementDownward(BlockPos src, BlockPos dest) : Movement(MovementType::Downward, src, dest, {dest}, std::nullopt) {}

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_}; }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        if (ctx.playerFeet() == dest_) return state.setStatus(MovementStatus::Success);
        if (!playerInValidPosition(ctx)) return state.setStatus(MovementStatus::Unreachable);
        Vec3d pos = ctx.player.position();
        double diffX = pos.x - (dest_.x + 0.5), diffZ = pos.z - (dest_.z + 0.5);
        double ab = std::sqrt(diffX * diffX + diffZ * diffZ);
        if (numTicks_++ < 10 && ab < 0.2) return state;
        mh::moveTowards(ctx, state, positionsToBreak_[0]);
        return state;
    }

private:
    int numTicks_ = 0;
};



// ===========================================================================
// Parkour
// ===========================================================================

class MovementParkour : public Movement {
public:
    MovementParkour(BlockPos src, BlockPos dest) : Movement(MovementType::Parkour, src, dest, {}, std::nullopt) {
        BlockPos d = dest - src;
        dist_ = std::max(std::abs(d.x), std::abs(d.z));
        stepX_ = (d.x > 0) - (d.x < 0);
        stepZ_ = (d.z > 0) - (d.z < 0);
        ascend_ = dest.y > src.y;
        diagonal_ = stepX_ != 0 && stepZ_ != 0;
    }

    bool safeToCancel(ExecContext&) const override { return currentState_.status != MovementStatus::Running; }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> set;
        for (int i = 0; i <= dist_; ++i)
            for (int y = 0; y < 2; ++y) {
                set.insert(src_.offset(stepX_ * i, y, stepZ_ * i));
                if (diagonal_ && i > 0) {  // the body crosses the corner cells of each step
                    set.insert(src_.offset(stepX_ * i, y, stepZ_ * (i - 1)));
                    set.insert(src_.offset(stepX_ * (i - 1), y, stepZ_ * i));
                }
            }
        return set;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        BlockPos feet = ctx.playerFeet();
        Vec3d pos = ctx.player.position();
        if (feet.y < src_.y) return state.setStatus(MovementStatus::Unreachable);  // we fell
        if (dist_ >= 4 || ascend_ || diagonal_) state.setInput(Input::Sprint, true);
        if (Settings::get().allowWalkOnMagmaBlocks && ctx.props(feet.below()).type == T::Magma) state.setInput(Input::Sneak, true);
        mh::moveTowards(ctx, state, dest_);

        const double norm = diagonal_ ? std::numbers::sqrt2 : 1.0;
        const double out = ((pos.x - (src_.x + 0.5)) * stepX_ + (pos.z - (src_.z + 0.5)) * stepZ_) / norm;
        BlockPos step{stepX_, 0, stepZ_};

        // Takeoff. Nothing here is instant: braking, walking back and accelerating each take ticks,
        // so they are separate phases decided from the MEASURED speed, one after the other:
        //   0 ready / not started, 1 brake (sneak) until stopped, 2 walk back to the run-up start,
        //   3 sprint forward and build speed. The Jump key is only produced in 0 or 3, and only so
        //   that the jump tick lands on the block edge (chain::jumpOutAt), never before it.
        bool buildingUp = false;
        if (ctx.player.onGround() && pos.y - src_.y < 0.0001) {
            const Vec3d v = ctx.player.velocity();
            const double speedNow = (v.x * stepX_ + v.z * stepZ_) / norm;  // signed, along the jump
            const double goal = std::min(chain::neededSpeed(dist_, norm, dest_.y - src_.y), chain::kSteadySprint);
            const bool ready = speedNow >= goal - 0.01;

            // where the run-up has to start so the speed is built by the time we reach the edge
            const BlockPos back = src_.offset(-stepX_, 0, -stepZ_);
            const bool hasBack = ctx.canWalkOn(back.below()) && ctx.canWalkThrough(back) && ctx.canWalkThrough(back.above());
            const double minOut = hasBack ? -1.1 * norm : -0.4 * norm;
            const double startOut = std::max(chain::jumpOutAt(norm, goal) - chain::runUpDistance(goal) - 0.1, minOut);
            constexpr double kTol = 0.04, kStopped = 0.03;

            if (runPhase_ == 0 && !ready) runPhase_ = (out <= startOut + kTol) ? 3 : 1;
            if (runPhase_ != 0 && ++runTicks_ > 120) runPhase_ = 3;  // never stall forever
            if (runPhase_ == 1 && std::fabs(speedNow) < kStopped) runPhase_ = (out > startOut + kTol) ? 2 : 3;
            if (runPhase_ == 2 && out <= startOut + kTol) runPhase_ = 1;  // arrived: stop first

            switch (runPhase_) {
                case 1:  // brake: sneaking is edge-safe, but it takes a few ticks to stop
                    buildingUp = true;
                    state.setInput(Input::Sneak, true).setInput(Input::Sprint, false);
                    state.setInput(Input::MoveForward, false).setInput(Input::MoveBack, false);
                    break;
                case 2:  // walk back to the start of the run-up
                    buildingUp = true;
                    state.setInput(Input::Sprint, false).setInput(Input::MoveForward, false);
                    state.setInput(Input::MoveBack, true);
                    break;
                case 3:  // run up: keep sprinting; the speed builds over the next ticks
                    buildingUp = true;
                    state.setInput(Input::Sprint, true).setInput(Input::MoveForward, true);
                    state.setInput(Input::MoveBack, false);
                    break;
                default: break;
            }
            if ((runPhase_ == 0 || runPhase_ == 3) && out >= chain::jumpOutAt(norm, speedNow)) {
                state.setInput(Input::Jump, true);  // at the edge; if still slow it is best effort
            }
        }

        if (!ctx.player.onGround() && pos.y - src_.y > 0.0001 && feet != src_) {
            // in the air (Bedrock carries a bit further than Java): keep facing the jump
            // direction, stop pushing once over the landing block, step back if past its middle
            // predict where we land without input (air drag 0.91, gravity), then push or brake
            // along the jump direction (a unit vector: 45 degrees on a diagonal)
            double along = out - dist_ * norm;
            Vec3d vel = ctx.player.velocity();
            double v = (vel.x * stepX_ + vel.z * stepZ_) / norm, vy = vel.y, y = pos.y, travel = 0;
            for (int t = 0; t < 40; ++t) {
                vy = (vy - 0.08) * 0.98;
                y += vy;
                v *= 0.91;
                travel += v;
                if (vy < 0 && y <= dest_.y) break;
            }
            double landing = along + travel;  // relative to the middle of the landing block
            float yaw = static_cast<float>(std::atan2(-stepX_, stepZ_) * 180.0 / 3.14159265358979);
            state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
            state.setInput(Input::MoveForward, landing < -0.35);
            state.setInput(Input::MoveBack, landing > 0.05);
            if (landing >= -0.35) state.setInput(Input::Sprint, false);
            // Chained jump: keep the speed up on touchdown, but do NOT hold Jump here. Hopping on
            // the first ground tick takes off wherever we happened to land (early, off the middle
            // of the block); the next Parkour jumps at the edge instead (see jumpOutAt above).
            if (ctx.next && ctx.next->type == MovementType::Parkour && ctx.next->src == dest_ && vel.y < 0 &&
                pos.y - dest_.y < 0.6) {
                const BlockPos nd = ctx.next->dest - ctx.next->src;
                const int nx = (nd.x > 0) - (nd.x < 0), nz = (nd.z > 0) - (nd.z < 0);
                const double nn = (nx != 0 && nz != 0) ? std::numbers::sqrt2 : 1.0;
                const double lat = (vel.x * nz - vel.z * nx) / nn;
                if (std::fabs(lat) < 0.06) {
                    state.setInput(Input::Sprint, true).setInput(Input::MoveForward, true);
                    state.setInput(Input::MoveBack, false);
                }
            }
            // Corner after this jump: like the Java preTurnIntoTheBend, once the landing is covered
            // face the next leg and steer in the air (strafe keys), so the momentum we touch down
            // with is already the entry angle instead of something to brake off on the ground.
            if (landing >= -0.35 && ctx.next && ctx.next->type == MovementType::Parkour && ctx.next->src == dest_) {
                const BlockPos nd = ctx.next->dest - ctx.next->src;
                const int nx = (nd.x > 0) - (nd.x < 0), nz = (nd.z > 0) - (nd.z < 0);
                if (chain::isCorner(stepX_, stepZ_, nx, nz)) {
                    const double nn = (nx != 0 && nz != 0) ? std::numbers::sqrt2 : 1.0;
                    const float nyaw = static_cast<float>(std::atan2(-nx, nz) * 180.0 / std::numbers::pi);
                    double px = nx / nn, pz = nz / nn;  // lean into the next axis
                    if (landing > 0.05) {               // and still shed any overshoot on this one
                        px -= stepX_ / norm;
                        pz -= stepZ_ / norm;
                    }
                    state.setTarget({Rotation{nyaw, ctx.playerRotations().pitch}, false});
                    chain::pushKeys(state, nyaw, px, pz);
                    state.setInput(Input::Sprint, false);
                }
            }
        }
        if (feet == dest_) {
            if (helper::isClimbable(ctx.props(dest_))) return state.setStatus(MovementStatus::Success);
            if (pos.y - feet.y < 0.094) {
                // Touched down: arrive in the state the next movement needs (see chain::settle)
                switch (chain::settle(ctx, state, dest_, ctx.next, landBrakeTicks_)) {
                    case chain::Settle::Done: return state.setStatus(MovementStatus::Success);
                    case chain::Settle::Settling: return state;
                    case chain::Settle::Impossible: return state.setStatus(MovementStatus::Unreachable);
                }
            }
        } else if (buildingUp) {
            // run-up in progress (see above): the fallbacks below would fight it
        } else if (diagonal_ && ctx.player.onGround() && pos.y - src_.y < 0.0001) {
            if (out < -0.2) {
                state.setInput(Input::Sprint, false);
                mh::moveTowards(ctx, state, src_);
            }
        } else if (feet != src_ && !diagonal_) {
            if (feet == src_ + step || pos.y - src_.y > 0.0001) {
                if (Settings::get().allowPlace && ctx.inventory.hasGenericThrowaway() && !ctx.canWalkOn(dest_.below()) &&
                    !ctx.player.onGround() &&
                    mh::attemptToPlaceABlock(ctx, state, dest_.below(), true, false) == mh::PlaceResult::ReadyToPlace) {
                    state.setInput(Input::ClickRight, true);
                }
                state.setInput(Input::Jump, true);
            } else if (feet != dest_ - step) {
                state.setInput(Input::Sprint, false);
                if (feet == src_ - step) mh::moveTowards(ctx, state, src_);
                else mh::moveTowards(ctx, state, src_ - step);
            }
        }
        return state;
    }

private:
    int dist_, stepX_, stepZ_;
    bool ascend_;
    bool diagonal_ = false;  // Bedrock addition: a jump along a diagonal
    int landBrakeTicks_ = 0;
    int runPhase_ = 0;  // takeoff phase: 0 ready, 1 brake, 2 walk back, 3 run up
    int runTicks_ = 0;
};

// ===========================================================================

std::unique_ptr<Movement> Movement::create(const MovementSpec& spec) {
    std::unique_ptr<Movement> m;
    switch (spec.type) {
        case MovementType::Traverse: m = std::make_unique<MovementTraverse>(spec.src, spec.dest); break;
        case MovementType::Ascend: m = std::make_unique<MovementAscend>(spec.src, spec.dest); break;
        case MovementType::Descend: m = std::make_unique<MovementDescend>(spec.src, spec.dest); break;
        case MovementType::Fall: m = std::make_unique<MovementFall>(spec.src, spec.dest); break;
        case MovementType::Diagonal: m = std::make_unique<MovementDiagonal>(spec.src, spec.dest); break;
        case MovementType::Pillar: m = std::make_unique<MovementPillar>(spec.src, spec.dest); break;
        case MovementType::Downward: m = std::make_unique<MovementDownward>(spec.src, spec.dest); break;
        case MovementType::Parkour: m = std::make_unique<MovementParkour>(spec.src, spec.dest); break;
    }
    m->overrideCost(spec.cost);
    m->calculatedWhileLoaded = spec.calculatedWhileLoaded;
    return m;
}

}  // namespace baritone
