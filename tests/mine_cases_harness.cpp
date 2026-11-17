// Second harness: the awkward cases.
//
// The first harness proves the happy path. This one proves the parts that are easy
// to get wrong and that the user will notice immediately: reaching a block that is
// not in the open, an ore buried in stone, and stopping once enough is collected.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "baritone/Baritone.h"
#include "baritone/cache/WorldScanner.h"
#include "tests/TestPlayer.h"

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++gFailures;
}

struct RunResult {
    int ticks = 0;
    bool sawClick = false;
    bool sawMove = false;
    bool finished = false;
    baritone::Vec3d endPos;
};

RunResult run(baritone::Baritone& bot, baritone_test::TestPlayer& player,
              const std::shared_ptr<baritone::WorldSnapshot>& world, int maxTicks) {
    RunResult r;
    for (; r.ticks < maxTicks; ++r.ticks) {
        bot.tick(world);
        player.applyControls(bot.controls());
        const auto& c = bot.controls();
        if (c.input(baritone::Input::ClickLeft)) r.sawClick = true;
        if (c.input(baritone::Input::MoveForward) || c.input(baritone::Input::MoveBack) ||
            c.input(baritone::Input::MoveLeft) || c.input(baritone::Input::MoveRight) ||
            c.input(baritone::Input::Jump))
            r.sawMove = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    r.endPos = player.position();
    return r;
}

}  // namespace

int main() {
    // ---------------------------------------------------------------- iron ore
    {
        std::printf("\n== iron ore buried in stone ==\n");
        auto world = baritone_test::flatWorld(-64, 320, -20);
        baritone_test::TestPlayer player;
        player.setPosition({8.5, -20.0, 8.5});
        player.setGroundY(-20.0);

        // Buried 7 blocks to the east, level with the player's feet.
        const baritone::BlockPos ore{15, -20, 8};
        baritone_test::place(world, ore, "minecraft:iron_ore");

        baritone::Baritone bot(player, [](const std::string& m, bool e) {
            std::printf("  [bot%s] %s\n", e ? "!" : "", m.c_str());
        });
        bot.tick(world);
        bot.mine().mine(0, baritone::BlockFilter({"minecraft:iron_ore"}));
        check(bot.mine().isActive(), "iron ore accepted");

        auto r = run(bot, player, world, 500);
        std::printf("  ran %d ticks, ended at (%.2f, %.2f, %.2f)\n", r.ticks, r.endPos.x,
                    r.endPos.y, r.endPos.z);
        check(r.sawMove, "bot moved toward the buried ore");
        check(r.sawClick, "bot tried to break through the stone");
    }

    // ------------------------------------------------------------ quantity stop
    // Two runs over the same world: one with a quantity (must stop by itself) and one
    // with 0 (means "keep going"). The earlier version of this section called
    // mine(0, ...) and claimed to test the quantity stop - it only tested movement.
    {
        std::printf("\n== stops after the requested quantity ==\n");
        auto world = baritone_test::flatWorld(-64, 320, -20);
        baritone_test::TestPlayer player;
        player.setPosition({8.5, -20.0, 8.5});
        player.setGroundY(-20.0);
        for (int x = 11; x <= 14; ++x) {
            baritone_test::place(world, {x, -19, 8}, "minecraft:sugarcane");
            baritone_test::place(world, {x, -20, 8}, "minecraft:sand");
        }

        baritone::Baritone bot(player, [](const std::string& m, bool e) {
            std::printf("  [bot%s] %s\n", e ? "!" : "", m.c_str());
        });
        bot.tick(world);
        // LIMITATION, stated instead of hidden: TestPlayer::countItem() always
        // returns 0, so MineProcess::countHave() can never reach a positive quantity
        // here and the "collected enough" stop cannot be observed in this harness.
        // That path is covered by tests/sim_harness.cpp, whose player counts drops
        // (quantity 1 -> completed after one raw iron). What this harness can prove
        // is that a quantity does not stop the bot from starting, and that it keeps
        // working while the filter still has known blocks.
        bot.mine().mine(2, baritone::BlockFilter({"minecraft:sugarcane"}));
        check(bot.mine().isActive(), "sugarcane accepted with quantity 2");

        auto r = run(bot, player, world, 260);
        std::printf("  ran %d ticks, ended at (%.2f, %.2f, %.2f)\n", r.ticks, r.endPos.x,
                    r.endPos.y, r.endPos.z);
        check(r.sawMove, "bot moved toward the sugarcane field");
        check(r.endPos.x > 9.5, "bot left its starting position");
        check(r.sawClick, "bot tried to break sugarcane");
        check(bot.mine().isActive(),
              "still mining: this fake player never counts items, see the note above");
    }

    // -------------------------------------------------------- zero means forever
    {
        std::printf("\n== quantity 0 keeps mining ==\n");
        auto world = baritone_test::flatWorld(-64, 320, -20);
        baritone_test::TestPlayer player;
        player.setPosition({8.5, -20.0, 8.5});
        player.setGroundY(-20.0);
        baritone_test::place(world, {14, -20, 8}, "minecraft:iron_ore");

        baritone::Baritone bot(player, [](const std::string& m, bool e) {
            std::printf("  [bot%s] %s\n", e ? "!" : "", m.c_str());
        });
        bot.tick(world);
        bot.mine().mine(0, baritone::BlockFilter({"minecraft:iron_ore"}));
        auto r = run(bot, player, world, 120);
        check(r.sawMove, "quantity 0 still starts mining");
        // It may finish when the filter is exhausted; what it must not do is refuse.
        check(bot.mine().isActive() || r.sawClick,
              "quantity 0 is not treated as \"mine nothing\"");
    }

    std::printf("\n== result ==\n%s\n", gFailures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return gFailures == 0 ? 0 : 1;
}
