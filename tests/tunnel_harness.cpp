// Tests for the tunnel and the aiming maths.
//
// Same caveat as the other harnesses: this is a fake world and a fake player, not
// Minecraft. What it can prove is that the process asks for the right things and
// that the aiming/raycast maths is self-consistent.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "baritone/Baritone.h"
#include "platform/AimRay.h"
#include "platform/MineAim.h"
#include "tests/TestPlayer.h"

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++gFailures;
}

}  // namespace

int main() {
    using namespace baritone::platform;

    // ---------------------------------------------------------------- aim maths
    {
        std::printf("\n== aim direction ==\n");
        // Standing at (8.5, -60, 8.5), eye 1.62 up. Look at a block due north (-Z).
        auto north = aim::toward(8.5, -58.38, 8.5, 8.5, -58.5, 7.5);
        check(std::fabs(north.yaw - 0.0f) < 1.0f, "block to the north gives yaw ~0");

        auto south = aim::toward(8.5, -58.38, 8.5, 8.5, -58.5, 9.5);
        check(std::fabs(std::fabs(south.yaw) - 180.0f) < 1.0f, "block to the south gives yaw ~180");

        auto east = aim::toward(8.5, -58.38, 8.5, 9.5, -58.5, 8.5);
        check(std::fabs(std::fabs(east.yaw) - 90.0f) < 1.0f, "block to the east/west gives yaw ~+-90");

        // A block above the eye should need a negative pitch (looking up).
        auto up = aim::toward(8.5, -58.38, 8.5, 8.5, -56.0, 8.5);
        check(up.pitch < -1.0f, "block above the eye gives a negative (upward) pitch");
    }

    // ------------------------------------------------------- two-stage aim closes
    {
        std::printf("\n== aim converges ==\n");
        LookAngles have{0.0f, 0.0f};
        const LookAngles want{170.0f, -25.0f};

        // 26 degrees of turn per event unit, cap 0.1 -> 2.6 deg per event at most.
        constexpr float kGain = 26.0f;
        constexpr float kMaxEvent = 0.1f;

        int ticks = 0;
        for (; ticks < 400 && !aim::onTarget(have, want, 1.0f); ++ticks) {
            const auto step = aim::step(have, want, kGain, kMaxEvent, 12.0f);
            have.yaw += step.x * kGain;
            have.pitch += step.y * kGain;
        }
        std::printf("  converged in %d aim steps, error %.2f deg\n", ticks,
                    aim::error(have, want));
        check(ticks < 400, "large turn completes within 400 steps");
        check(aim::error(have, want) < 1.0f, "ends within 1 degree on both axes");
        check(ticks > 20, "does not teleport: takes many small steps, as the game requires");
    }

    // ------------------------------------------------------------- raycast exact
    {
        std::printf("\n== raycast ==\n");
        // A single solid block at (8, -59, 7); everything else air.
        const auto isSolid = [](int x, int y, int z) {
            return x == 8 && y == -59 && z == 7;
        };
        // Eye at the centre of (8, -58, 8) looking due north.
        auto hit = raycastBlocks(8.5, -58.5, 8.5, 0.0f, 0.0f, 5.0, isSolid);
        check(hit.has_value(), "ray hits the block straight ahead");
        if (hit) {
            check(hit->x == 8 && hit->y == -59 && hit->z == 7, "hit is the expected block");
            // Travelling -Z, the first face reached is the +Z face of the block.
            check(std::fabs(hit->normalZ - 1.0) < 1e-9, "reports the +Z face it entered through");
            check(std::fabs(hit->distance - 0.5) < 1e-6,
                  "distance is exactly the near face at 0.5 blocks");
        }

        // Nothing there: the ray must not invent a hit.
        auto miss = raycastBlocks(8.5, -58.5, 8.5, 0.0f, 0.0f, 5.0, [](int, int, int) {
            return false;
        });
        check(!miss.has_value(), "empty world gives no hit");

        // Occlusion: with solids at both z=9 and z=10 looking south, the nearer one
        // must be reported. This is what stops a tunnel click landing past the wall.
        const auto twoWalls = [](int, int, int z) { return z == 9 || z == 10; };
        auto nearWall = raycastBlocks(8.5, -58.5, 8.5, 180.0f, 0.0f, 5.0, twoWalls);
        check(nearWall && nearWall->z == 9, "ray stops at the nearest solid block");

        // Reach is respected: a block past maxDistance must not be reported.
        auto outOfReach = raycastBlocks(8.5, -58.5, 8.5, 180.0f, 0.0f, 0.4, twoWalls);
        check(!outOfReach.has_value(), "block beyond reach is not reported");
    }

    // ------------------------------------------------------------ tunnel 1x2
    {
        std::printf("\n== tunnel ==\n");
        // Flat ground with air above, and a solid wall to the south that is 2 blocks
        // high. A 1x2 tunnel has to clear both layers, which is the point.
        auto world = baritone_test::flatWorld(-64, 320, -58);
        baritone_test::TestPlayer player;
        player.setPosition({8.5, -58.0, 8.5});
        player.setGroundY(-58.0);
        for (int x = 0; x < 16; ++x) {
            baritone_test::place(world, {x, -58, 9}, "minecraft:stone");
            baritone_test::place(world, {x, -57, 9}, "minecraft:stone");
        }
        check(baritone_test::blockName(world, {8, -57, 9}) == "minecraft:stone",
              "wall is 2 blocks high in front of the player");

        // Only the air layers are passable, and the wall is not.
        player.setPassable([](int x, int y, int z) {
            if (y < -58) return false;
            if (z == 9 && y <= -57) return false;
            return true;
        });

        baritone::Baritone bot(player, [](const std::string& m, bool e) {
            std::printf("  [bot%s] %s\n", e ? "!" : "", m.c_str());
        });
        bot.tick(world);
        bot.tunnel().start(baritone::Direction::South, 4);
        check(bot.tunnel().isActive(), "tunnel starts");

        bool sawClick = false;
        int clicks = 0;
        int ticks = 0;
        for (; ticks < 200 && bot.tunnel().isActive(); ++ticks) {
            bot.tick(world);
            player.applyControls(bot.controls());
            if (bot.controls().input(baritone::Input::ClickLeft)) {
                sawClick = true;
                ++clicks;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        std::printf("  ran %d ticks, %d click ticks, ended at (%.2f, %.2f, %.2f)\n", ticks,
                    clicks, player.position().x, player.position().y, player.position().z);
        check(sawClick, "tunnel asks to break the wall in front of it");
        check(player.position().z < 9.0, "bot does not walk through the unbroken wall");

        bot.tunnel().start(baritone::Direction::South, 4);
        bot.tunnel().cancel();
        check(!bot.tunnel().isActive(), "cancel stops the tunnel");
    }

    std::printf("\n== result ==\n%s\n", gFailures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return gFailures == 0 ? 0 : 1;
}
