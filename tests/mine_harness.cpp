// Headless test harness for baritone's MineProcess.
//
// Builds a flat world with a few sugarcane blocks and a fake player, then runs
// MineProcess the way the mod would: set a filter, tick, and record what the bot
// decides to do each tick. No game, no hooks, no device.
//
// Purpose: prove the pathfinding + mining decision loop actually works before
// touching the Android layer, and give a place to prove it still works after.

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "baritone/Baritone.h"
#include "baritone/api/PlayerContext.h"
#include "baritone/cache/WorldScanner.h"
#include "tests/TestPlayer.h"

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++gFailures;
}

}  // namespace

int main() {
    auto world = baritone_test::flatWorld(-64, 320, /*groundY=*/-60);
    baritone_test::TestPlayer player;
    player.setPosition({8.5, -60.0, 8.5});
    player.setGroundY(-60.0);

    // Sugarcane standing on the ground, 5 blocks away.
    const baritone::BlockPos cane{12, -59, 8};
    baritone_test::place(world, cane, "minecraft:sugarcane");
    baritone_test::place(world, {12, -60, 8}, "minecraft:sand");

    baritone::Baritone bot(player, [](const std::string& msg, bool err) {
        std::printf("  [bot%s] %s\n", err ? "!" : "", msg.c_str());
    });

    std::printf("\n== setup ==\n");
    check(baritone_test::blockName(world, cane) == "minecraft:sugarcane", "sugarcane placed in world");

    std::printf("\n== scan ==\n");
    baritone::BlockFilter filter({"minecraft:sugarcane"});
    auto found = baritone::scanLoadedChunks(*world, filter, {8, -60, 8}, 16, 4);
    check(!found.empty(), "WorldScanner finds the sugarcane");
    if (!found.empty()) {
        std::printf("  found %zu, nearest = (%d, %d, %d)\n", found.size(), found[0].x, found[0].y,
                    found[0].z);
        check(found[0] == cane, "scan returns the exact block");
    }

    std::printf("\n== MineProcess ==\n");
    // The mod ticks first (so the world snapshot exists) and only then accepts a command.
    bot.tick(world);
    bot.mine().mine(0, filter);
    check(bot.mine().isActive(), "MineProcess accepts the filter and becomes active");

    int ticks = 0;
    bool sawClick = false;
    bool sawMove = false;
    bool sawRotation = false;
    for (; ticks < 400; ++ticks) {
        bot.tick(world);
        player.applyControls(bot.controls());
        // Path calculation runs on a background thread, so a tick loop with no real
        // time between ticks never lets it finish. 5 ms is faster than 20 TPS but far
        // from instant.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto& c = bot.controls();
        if (c.input(baritone::Input::ClickLeft)) sawClick = true;
        if (c.input(baritone::Input::MoveForward) || c.input(baritone::Input::Jump) ||
            c.input(baritone::Input::MoveLeft) || c.input(baritone::Input::MoveRight))
            sawMove = true;
        if (c.target.rotation) sawRotation = true;
        if (!bot.mine().isActive()) break;
    }
    std::printf("  ran %d ticks, ended at (%.2f, %.2f, %.2f)\n", ticks, player.position().x,
                player.position().y, player.position().z);
    check(sawRotation, "bot produced a target rotation (it aims)");
    check(sawMove || sawClick, "bot produced movement or a click");
    check(sawClick, "bot produced Input::ClickLeft (wants to break)");
    // Evidence for the doc: which break path did the core actually take?
    std::printf("  PlayerContext::startOrContinueBreaking calls: %zu\n",
                player.breakRequests.size());
    check(player.breakRequests.empty(),
          "break request went through Input::ClickLeft, not PlayerContext");

    std::printf("\n== result ==\n");
    std::printf("%s\n", gFailures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return gFailures == 0 ? 0 : 1;
}
