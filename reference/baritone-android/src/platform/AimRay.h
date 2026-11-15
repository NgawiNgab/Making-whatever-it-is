#pragma once
// Voxel raycast from the player's eye to a block, at crosshair resolution.
//
// The core's `reachable()` gives a rotation that is *somewhere* on the block. That is
// enough to look like it is aiming, but not enough to break a block reliably:
//
//   * The click has to land inside the block's real box, and a 1x2 tunnel means the
//     block above feet level and the one in front are different targets.
//   * If another block is in the way, the click hits that instead - and the bot then
//     mines something it did not plan to, which is how a tunnel turns into a crater.
//
// So: march the grid, find the first block hit, report which face and where on it.
// Pure function of a block predicate, so it is testable without a game.

#include <cmath>
#include <functional>
#include <limits>
#include <optional>

#include "baritone/utils/BlockPos.h"

namespace baritone::platform {

struct AimHit {
    int x = 0, y = 0, z = 0;
    // Point on the face that was hit, in world units.
    double hitX = 0.0, hitY = 0.0, hitZ = 0.0;
    // Unit normal of the face that was hit, e.g. (0, 1, 0) for the top face.
    double normalX = 0.0, normalY = 0.0, normalZ = 0.0;
    double distance = 0.0;
};

// Amanatides-Woo voxel traversal.
//   eye/angles  : where the crosshair starts and what direction it points
//   isSolid     : true if the block at (x,y,z) can be hit (not air, not fluid)
//   maxDistance : reach
// Returns the first solid block along the ray, or nullopt if the ray reaches
// maxDistance without hitting anything.
//
// `angles` follows this codebase's convention: yaw 0 = -Z, pitch + = down.
inline std::optional<AimHit> raycastBlocks(double eyeX, double eyeY, double eyeZ, float yaw,
                                           float pitch, double maxDistance,
                                           const std::function<bool(int, int, int)>& isSolid) {
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    const double yawRad = yaw * kDeg;
    const double pitchRad = pitch * kDeg;

    // Direction: yaw turns the horizontal plane, pitch tilts it.
    const double horizontal = std::cos(pitchRad);
    const double dirX = -std::sin(yawRad) * horizontal;
    const double dirY = -std::sin(pitchRad);
    const double dirZ = -std::cos(yawRad) * horizontal;

    int x = static_cast<int>(std::floor(eyeX));
    int y = static_cast<int>(std::floor(eyeY));
    int z = static_cast<int>(std::floor(eyeZ));

    const int stepX = dirX > 0 ? 1 : (dirX < 0 ? -1 : 0);
    const int stepY = dirY > 0 ? 1 : (dirY < 0 ? -1 : 0);
    const int stepZ = dirZ > 0 ? 1 : (dirZ < 0 ? -1 : 0);

    // Distance along the ray to the next voxel boundary on each axis.
    const auto firstBoundary = [](double start, int cell, int step, double dir) {
        if (step == 0 || dir == 0.0) return std::numeric_limits<double>::infinity();
        const double boundary = static_cast<double>(step > 0 ? cell + 1 : cell);
        return (boundary - start) / dir;
    };
    const auto deltaFor = [](int step, double dir) {
        if (step == 0 || dir == 0.0) return std::numeric_limits<double>::infinity();
        return std::fabs(1.0 / dir);
    };

    double tMaxX = firstBoundary(eyeX, x, stepX, dirX);
    double tMaxY = firstBoundary(eyeY, y, stepY, dirY);
    double tMaxZ = firstBoundary(eyeZ, z, stepZ, dirZ);
    const double tDeltaX = deltaFor(stepX, dirX);
    const double tDeltaY = deltaFor(stepY, dirY);
    const double tDeltaZ = deltaFor(stepZ, dirZ);

    // The block the eye is inside is not a hit: that is the player's own space.
    //
    // Note the normal: `axis` says which boundary was *crossed to enter* the cell we
    // are now in, so the face we came in through points back along the ray. Reporting
    // the exit face instead would put the click on the far side of the block.
    double t = 0.0;
    int guard = 0;
    while (guard++ < 1024) {
        const int axis = (tMaxX < tMaxY) ? (tMaxX < tMaxZ ? 0 : 2) : (tMaxY < tMaxZ ? 1 : 2);
        t = (axis == 0) ? tMaxX : (axis == 1) ? tMaxY : tMaxZ;
        if (t > maxDistance) break;

        AimHit hit;
        hit.distance = t;

        if (axis == 0) {
            hit.normalX = -static_cast<double>(stepX);
            tMaxX += tDeltaX;
            x += stepX;
        } else if (axis == 1) {
            hit.normalY = -static_cast<double>(stepY);
            tMaxY += tDeltaY;
            y += stepY;
        } else {
            hit.normalZ = -static_cast<double>(stepZ);
            tMaxZ += tDeltaZ;
            z += stepZ;
        }

        hit.x = x;
        hit.y = y;
        hit.z = z;
        if (!isSolid(x, y, z)) continue;

        // Pull the reported point just inside the face so it is unambiguous which
        // side of the boundary the click lands on.
        hit.hitX = eyeX + dirX * t + hit.normalX * 1e-3;
        hit.hitY = eyeY + dirY * t + hit.normalY * 1e-3;
        hit.hitZ = eyeZ + dirZ * t + hit.normalZ * 1e-3;
        return hit;
    }
    return std::nullopt;
}

}  // namespace baritone::platform
