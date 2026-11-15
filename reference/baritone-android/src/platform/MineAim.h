#pragma once
// Turning a block the bot wants to break into something it can actually send.
//
// Two separate problems, both solved here so the game side in InputOverride only has
// to call one function:
//
//  1. **Which block.** The core never says "break (12, -59, 8)". It says "click
//     left" and puts a rotation in `controls().target.rotation` aimed at the block
//     (MineProcess.cpp:362, Movement.cpp:508). The player context path
//     (`startOrContinueBreaking`) is dead code in this port - the core never calls
//     it - so the aimed block has to be recovered from the rotation, by raycast.
//
//  2. **Which pixel.** Bedrock caps how far one `InputEventQueue::enqueueDirection`
//     may turn the camera, and the camera already lags a tick behind. So aiming has
//     to be a closed loop over several ticks, not one jump to the target yaw:
//        coarse event  -> big step, used until close
//        fine event    -> small step, for the last couple of degrees
//        lookDelta     -> direct write, only once nearly aimed
//
// Research only touches the rotation the core produced, so this compiles and is
// testable without a game.
//
// Assumes: rotation {pitch: +down, yaw}; forward is (-sin(yaw), -cos(yaw)).

#include <algorithm>
#include <cmath>
#include <optional>

#include "baritone/utils/Rotation.h"

namespace baritone::platform {

// A rotation that makes the crosshair hit something.
struct LookAngles {
    float yaw = 0.0f;    // 0 = south (-Z), increases counter-clockwise
    float pitch = 0.0f;  // + looks down
};

namespace aim {

inline constexpr float kDeg = 3.14159265358979323846f / 180.0f;

// Vector from an eye position to a block centre, as look angles.
inline LookAngles toward(double eyeX, double eyeY, double eyeZ, double blockX, double blockY,
                         double blockZ) {
    const double dx = blockX - eyeX;
    const double dy = blockY - eyeY;
    const double dz = blockZ - eyeZ;
    LookAngles out;
    const double horizontal = std::sqrt(dx * dx + dz * dz);
    out.yaw = static_cast<float>(std::atan2(-dx, -dz) / kDeg);
    out.pitch = static_cast<float>(-std::atan2(dy, horizontal) / kDeg);
    return out;
}

// Signed difference a-b, wrapped to (-180, 180].
inline float delta(float a, float b) {
    float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
    return d;
}

// How far apart two angles are (worst axis).
inline float error(const LookAngles& have, const LookAngles& want) {
    return std::max(std::fabs(delta(want.yaw, have.yaw)), std::fabs(want.pitch - have.pitch));
}

// One step of a two-stage aim. `gain` is the game's degrees-turn-per-event-unit and
// `maxEvent` the largest value the queue accepts; the ratio between the two stages
// is what makes this close the last few degrees instead of overshooting.
//
// coarse: full deflection while far away, for a fast swing round
// fine:   scaled down so one event lands within `fineTolerance` degrees
struct EventStep {
    float x = 0.0f;
    float y = 0.0f;
    float remaining = 0.0f;  // degrees still to go, for logging
};

inline EventStep step(const LookAngles& have, const LookAngles& want, float gain, float maxEvent,
                      float fineTolerance) {
    const float dyaw = delta(want.yaw, have.yaw);
    const float dpitch = want.pitch - have.pitch;
    const float remaining = std::max(std::fabs(dyaw), std::fabs(dpitch));

    EventStep out;
    out.remaining = remaining;
    if (gain <= 0.0f || maxEvent <= 0.0f) return out;

    // Turn per event unit, in degrees.
    const float perUnit = gain;
    const bool fine = remaining < fineTolerance;
    const float limit = fine ? maxEvent * 0.2f : maxEvent;

    out.x = std::clamp(dyaw / (perUnit * 8.0f), -limit, limit);
    out.y = std::clamp(dpitch / (perUnit * 8.0f), -limit, limit);
    return out;
}

// Close enough to stop turning and let clicks land.
inline bool onTarget(const LookAngles& have, const LookAngles& want, float tolerance) {
    return error(have, want) <= tolerance;
}

}  // namespace aim
}  // namespace baritone::platform
