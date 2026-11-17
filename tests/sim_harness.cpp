// Regression harness for the first-party headless simulation engine (src/sim).
//
// Everything here is deterministic and in-memory: no game process, no network, no
// clock-dependent assertion (wall/CPU/RSS are only range-checked). Each check states
// the property it proves so a failure names the broken guarantee.
//
//   ./tests/run.sh            (builds and runs this with the other harnesses)
//   build/bin/sim_harness     (same test linked against the CMake static libraries)

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "sim/json.h"
#include "sim/runner.h"
#include "sim/scenario.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::cout << "  [fail] " << what << "\n";
    } else {
        std::cout << "  [ ok ] " << what << "\n";
    }
}

void section(const char* name) { std::cout << "\n== " << name << " ==\n"; }

bool throws(const std::function<void()>& fn, const std::string& needle = {}) {
    try {
        fn();
    } catch (const std::exception& error) {
        if (needle.empty()) return true;
        return std::string(error.what()).find(needle) != std::string::npos;
    }
    return false;
}

mwi::sim::Scenario parseScenario(const std::string& text) {
    return mwi::sim::Scenario::fromJson(mwi::json::parse(text));
}

// A mine scenario: `block` sits `distance` blocks east of the player's feet at the
// player's own height, on a stone floor. Small numbers keep the default chunk
// radius honest; the block is past the search radius only when asked for.
std::string mineJson(const std::string& block, int distance, int tier, double speed,
                     int quantity = 1, int maxTicks = 700, int breakTicks = 1,
                     int dimension = 0, int blocksY = -20) {
    return std::string(R"({
      "name": "harness-mine", "mode": "mine",
      "dimension": )") + std::to_string(dimension) + R"(,
      "min_y": -64, "max_y": 320, "ground_y": -20, "chunk_radius": 1, "search_radius": 16,
      "player": {"x": 8.5, "y": -20.0, "z": 8.5, "yaw": -90, "pitch": 0, "reach": 4.5},
      "hotbar": [{"name": "minecraft:test_pickaxe", "count": 1, "tool": "pickaxe",
                  "tier": )" + std::to_string(tier) + R"(, "speed": )" + std::to_string(speed) + R"(, "max_damage": 250}],
      "blocks": [{"x": )" + std::to_string(8 + distance) + R"(, "y": )" + std::to_string(blocksY) +
           R"(, "z": 8, "name": ")" + block + R"("}],
      "targets": [")" + block + R"("],
      "quantity": )" + std::to_string(quantity) + R"(,
      "max_ticks": )" + std::to_string(maxTicks) + R"(,
      "tick_delay_ms": 1, "break_ticks": )" + std::to_string(breakTicks) + R"(
    })";
}

// Same layout, but a tunnel world is solid rock from min_y through ground_y with a
// two-block pocket at the player, so the corridor has to be carved. (Mine worlds are
// an open surface: the player stands on a floor, not inside stone.)
std::string tunnelJson(int yaw, int length, int groundY = 38) {
    return std::string(R"({
      "name": "harness-tunnel", "mode": "tunnel",
      "min_y": -64, "max_y": 320, "ground_y": )") + std::to_string(groundY) + R"(, "chunk_radius": 1,
      "player": {"x": 8.5, "y": )" + std::to_string(groundY) + R"(.0, "z": 8.5, "yaw": )" +
           std::to_string(yaw) + R"(, "pitch": 0, "reach": 4.5},
      "hotbar": [{"name": "minecraft:iron_pickaxe", "count": 1, "tool": "pickaxe", "tier": 3, "speed": 6.0}],
      "length": )" + std::to_string(length) + R"(, "max_ticks": 1200, "tick_delay_ms": 1, "break_ticks": 1
    })";
}

void testJson() {
    section("json");
    using mwi::json::parse;

    check(parse(R"({"a": 1, "b": [true, null, "x"], "c": {"d": -2.5e2}})")
              .get("c")->get("d")->asDouble() == -250.0,
          "parses nested objects, arrays, exponents and signs");
    check(throws([] { parse(""); }, " at byte "), "empty input is rejected with a byte position");
    check(throws([] { parse(R"({"a": 1, "a": 2})"); }, "duplicate"),
          "duplicate object keys are rejected");
    check(throws([] { parse(R"({"a": 01})"); }), "leading zeros are rejected");
    check(throws([] { parse(R"({"a": NaN})"); }), "NaN is not JSON");
    check(throws([] { parse(R"({"a": "unterminated})"); }), "unterminated string is rejected");
    check(throws([] { parse("[1, 2,]"); }), "trailing comma is rejected");
    check(throws([] { parse(R"({"a": 1} trailing)"); }, " at byte "),
          "trailing garbage after the root value is rejected");
    check(throws([] { parse("\"\xff\""); }, "UTF-8"), "invalid UTF-8 in a string is rejected");
    {
        std::string deep(600, '[');
        deep += std::string(600, ']');
        check(throws([&] { parse(deep); }), "excessive nesting is rejected instead of overflowing the stack");
    }

    // Round trip: a value written by stringify() parses back to the same numbers and
    // strings, pretty and compact.
    const auto original = parse(R"({"n": 1.5, "s": "line\nquote\"", "b": false, "a": [1, 2]})");
    for (bool pretty : {false, true}) {
        const auto again = parse(mwi::json::stringify(original, pretty));
        check(again.get("n")->asDouble() == 1.5 && again.get("s")->asString() == "line\nquote\"" &&
                  again.get("b")->asBool() == false && again.get("a")->asArray().size() == 2,
              pretty ? "pretty JSON round-trips" : "compact JSON round-trips");
    }
    check(mwi::json::stringify(original, false).find('\n') == std::string::npos,
          "compact output stays on one line");
}

void testScenarioSchema() {
    section("scenario schema");
    using mwi::sim::Mode;

    const auto mine = parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0));
    check(mine.mode == Mode::Mine && mine.targets.size() == 1 &&
              mine.targets[0] == "minecraft:iron_ore",
          "mine scenario parses mode/targets");
    check(mwi::sim::normalizeBlockName("Iron_Ore") == "minecraft:iron_ore",
          "block names are lowercased and namespaced");

    const auto withSearch = parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0));
    check(withSearch.searchRadius == 16, "search_radius is parsed");

    // Absent, unknown, and out-of-range input must fail loudly rather than default.
    check(throws([] { parseScenario(R"({"mode": "mine"})"); }, "targets"),
          "mine scenarios without targets are rejected");
    check(throws([] { parseScenario(R"({"mode": "dig"})"); }, "mode"),
          "unknown mode is rejected");
    check(throws([] { parseScenario(R"({"mode": "tunnel", "min_y": -50})"); }, "multiple of 16"),
          "min_y must align to a chunk section");
    check(throws([] { parseScenario(R"({"mode": "tunnel", "ground_y": 400})"); }, "ground_y"),
          "ground_y outside the world is rejected");
    check(throws([] { parseScenario(R"({"mode": "tunnel", "length": 300})"); }, "length"),
          "length above the 256 block cap is rejected");
    check(throws([] { parseScenario(R"({"mode": "mine", "targets": ["stone"], "search_radius": 65})"); },
                 "search_radius"),
          "search_radius above 64 is rejected");
    check(throws([] { parseScenario(R"({"mode": "mine", "targets": ["stone"], "player": {"reach": 0}})");
                 }, "reach"),
          "zero reach is rejected");
    check(throws([] { parseScenario(R"({"mode": "mine", "targets": ["stone"],
                                      "hotbar": [{"name": "x", "tier": 9}]})"); }, "tier"),
          "tier above the 0-5 tool scale is rejected");
    check(throws([] { parseScenario(R"({"mode": "mine", "targets": ["stone"],
                                      "hotbar": [{"name": "x", "tool": "laser"}]})"); }, "tool"),
          "unknown tool kind is rejected");
    check(throws([] { parseScenario(R"({"mode": "mine", "targets": ["stone"], "break_ticks": 0})"); },
                 "break_ticks"),
          "break_ticks must be at least one tick");
}

void testDirectionConvention() {
    section("yaw to compass (core convention: 0 = south/+Z)");
    using mwi::sim::Scenario;
    struct Case {
        double yaw;
        const char* name;
        double dx, dz;
    };
    // Proves the mapping agrees with rotation_utils::calcLookDirection, not with the
    // old TunnelProcess table that had north and south swapped.
    const Case cases[] = {
        {0, "south", 0, 1}, {90, "west", -1, 0}, {180, "north", 0, -1}, {270, "east", 1, 0},
        {-90, "east", 1, 0}, {359, "south", 0, 1}, {44, "south", 0, 1}, {134, "west", -1, 0},
        {224, "north", 0, -1}, {314, "east", 1, 0},
    };
    for (const Case& c : cases) {
        Scenario s;
        s.mode = mwi::sim::Mode::Tunnel;
        s.yaw = c.yaw;
        s.length = 4;
        const auto delta = mwi::sim::directionDelta(s);
        const std::string what = "yaw " + std::to_string(c.yaw) + " faces " + c.name;
        check(mwi::sim::directionName(s) == c.name && delta.x == c.dx * 4 && delta.z == c.dz * 4,
              what);
    }
    Scenario explicitDir;
    explicitDir.direction = "east";
    explicitDir.length = 10;
    check(explicitDir.length == 10 && mwi::sim::directionName(explicitDir) == "east",
          "an explicit direction overrides yaw");
}

void testMiningHappyPath() {
    section("mining: happy path");
    const auto result = mwi::sim::runScenario(parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0)));

    check(result.status == "completed" && result.finished,
          "an exposed iron ore completes");
    check(result.breaks.size() == 1 && result.breaks[0].block == "minecraft:iron_ore" &&
              result.breaks[0].targetHit,
          "exactly the target block is broken");
    check(result.breaks[0].drop == "minecraft:raw_iron" && result.inventory.at("minecraft:raw_iron") == 1,
          "an iron pickaxe yields one raw iron");
    check(result.playerMetrics.targetBlocksBroken == 1 && result.playerMetrics.obstacleBlocksBroken == 0,
          "no obstacle blocks were broken on the way");
    check(result.playerMetrics.unresolvedClicks == 0 && result.playerMetrics.falsePositiveSuspects == 0,
          "no click missed and nothing looked like a false positive");
    check(result.initialTargets == 1 && result.remainingTargets == 0,
          "target counters start at 1 and end at 0");
    check(result.breaks[0].tick > 0 && result.ticks <= result.scenario.maxTicks,
          "the break happens inside the tick budget");
    check(result.scenario.modeName() == std::string("mine"), "result echoes the mode");
}

void testToolTiers() {
    section("mining: harvest levels (PlayerContext scale 1 wood .. 5 netherite)");
    struct Case {
        int tier;
        double speed;
        const char* block;
        const char* expectedDrop;  // "" = breaks but drops nothing
    };
    const Case cases[] = {
        {0, 1.0, "minecraft:stone", ""},
        {1, 2.0, "minecraft:stone", "minecraft:cobblestone"},
        {1, 2.0, "minecraft:iron_ore", ""},
        {2, 4.0, "minecraft:iron_ore", "minecraft:raw_iron"},
        {2, 4.0, "minecraft:diamond_ore", ""},
        {3, 6.0, "minecraft:diamond_ore", "minecraft:diamond"},
        {3, 6.0, "minecraft:obsidian", ""},
        {4, 8.0, "minecraft:obsidian", "minecraft:obsidian"},
    };
    for (const Case& c : cases) {
        const auto result = mwi::sim::runScenario(
            parseScenario(mineJson(c.block, 4, c.tier, c.speed)));
        const std::string label = std::string("tier ") + std::to_string(c.tier) + " on " + c.block;
        if (result.breaks.empty()) {
            const bool explained = result.status == "tool_insufficient" && result.toolInsufficient &&
                                   result.requiredToolTier > c.tier;
            check(explained, label + ": nothing broke and the run says the tool is too weak");
            continue;
        }
        const auto& event = result.breaks[0];
        if (*c.expectedDrop == '\0') {
            check(event.drop.empty() && !event.toolSatisfied && !event.note.empty(),
                  label + " breaks without a drop and explains why");
        } else {
            check(event.drop == c.expectedDrop && event.toolSatisfied,
                  label + " drops " + c.expectedDrop);
        }
    }
}

void testDimensionRules() {
    section("dimension rules");
    // Nether-only blocks (ancient debris) cannot appear in an overworld scenario, so
    // their harvest levels are checked here, in dimension 1, rather than in the
    // overworld tier table above.
    {
        auto lowTier = parseScenario(mineJson("minecraft:ancient_debris", 4, 3, 6.0, 1, 800, 1, 1, 32));
        lowTier.minY = 0;
        lowTier.maxY = 128;
        lowTier.groundY = 32;
        lowTier.player.y = 32.0;
        const auto result = mwi::sim::runScenario(lowTier);
        check(result.status == "tool_insufficient" && result.breaks.empty(),
              "an iron pickaxe cannot harvest ancient debris even in the nether");
    }
    {
        // A nether scenario must run without the "wrong dimension" warning. The block
        // is placed through the parsed model on purpose: MineBlockScenario::fromJson in
        // the overworld rejects ancient debris, which is checked separately below.
        // Parsed with dimension 1: the parser rejects ancient debris in the overworld,
        // which is checked in the next statement.
        auto scenario = parseScenario(mineJson("minecraft:ancient_debris", 4, 4, 8.0, 1, 800, 1, 1, 32));
        scenario.minY = 0;
        scenario.maxY = 128;
        scenario.groundY = 32;
        scenario.player.y = 32.0;
        const auto result = mwi::sim::runScenario(scenario);
        check(result.status == "completed" && result.breaks.size() == 1 &&
                  result.breaks[0].drop == "minecraft:ancient_debris",
              "ancient debris in the nether drops debris");
        bool warnedAboutNether = false;
        for (const auto& line : result.logs) {
            if (line.message.find("Nether ore") != std::string::npos) warnedAboutNether = true;
        }
        check(!warnedAboutNether, "the nether scenario produces no dimension warning");
    }
    check(throws([] {
              parseScenario(mineJson("minecraft:ancient_debris", 4, 4, 8.0));
          }, "nether"),
          "ancient debris in the overworld is rejected with the fix in the message");
    check(throws([] {
              parseScenario(R"({"mode": "tunnel", "dimension": 3})");
          }, "dimension"),
          "an unknown dimension id is rejected");
}

void testNoTargetAndBlocked() {
    section("mining: absent vs blocked");
    {
        auto scenario = parseScenario(mineJson("minecraft:diamond_ore", 4, 3, 6.0));
        scenario.blocks.clear();  // still generated stone: the ore is simply not there
        const auto result = mwi::sim::runScenario(scenario);
        check(result.status == "no_target" && result.finished,
              "a missing block reports no_target");
        check(result.initialTargets == 0 && !result.falseNegativeSuspect,
              "no_target is not counted as a false negative");
    }
    {
        // Buried five blocks down: inside the search radius, outside reach.
        auto scenario = parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0));
        scenario.blocks[0] = {14, -26, 8, "minecraft:iron_ore"};
        const auto result = mwi::sim::runScenario(scenario);
        // Either the bot refuses as unreachable (blocked) or it runs out of budget;
        // what must never happen is a false "the world has no such block".
        check(result.status == "blocked" || result.status == "timeout",
              "a buried ore is never reported as absent");
        check(result.playerMetrics.targetBlocksBroken == 0 && result.falseNegativeSuspect,
              "no target broken while the block exists flags a false-negative suspect");
        check(result.blockedTargets == 1 && result.obscuredTargets == 1,
              "the blocked and obscured counts are attributed");
    }
    {
        // Same ore, but the search radius cannot see it: the run is blocked with no
        // attribution, never a false "the world has none".
        auto scenario = parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0));
        scenario.searchRadius = 0;
        scenario.blocks[0] = {14, -26, 8, "minecraft:iron_ore"};
        const auto result = mwi::sim::runScenario(scenario);
        check((result.status == "blocked" || result.status == "timeout") &&
                  result.blockedTargets == 0,
              "an unattributed miss never claims a blocked target");
    }
}

void testQuantityAndInventory() {
    section("mining: quantity and inventory accounting");
    {
        auto scenario = parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0, 1));
        scenario.blocks = {
            {12, -20, 8, "minecraft:iron_ore"},
            {14, -20, 8, "minecraft:iron_ore"},
            {16, -20, 8, "minecraft:iron_ore"},
        };
        const auto result = mwi::sim::runScenario(scenario);
        check(result.status == "completed" && result.inventory.at("minecraft:raw_iron") == 1,
              "quantity 1 stops after one iron");
        check(result.initialTargets == 3 && result.remainingTargets == 2,
              "remaining targets drop from 3 to 2");
    }
    {
        auto scenario = parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0, 0));
        scenario.blocks = {{12, -20, 8, "minecraft:iron_ore"}, {14, -20, 8, "minecraft:iron_ore"}};
        const auto result = mwi::sim::runScenario(scenario);
        check(result.status == "completed" && result.inventory.at("minecraft:raw_iron") == 2,
              "quantity 0 mines every reachable target, then completes");
    }
    {
        // The core's quantity is "have N of the drop in total", so a pre-existing stack
        // is part of the count and the run completes without mining anything.
        auto scenario = parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0, 1));
        scenario.inventory["minecraft:raw_iron"] = 7;
        const auto result = mwi::sim::runScenario(scenario);
        check(result.inventory.at("minecraft:raw_iron") == 7 && result.breaks.empty(),
              "an existing stack satisfies quantity without mining");
        check(result.status == "completed", "that run is reported completed");
    }
}

void testTunnel() {
    section("tunnel");
    struct Case {
        int yaw;
        const char* axis;  // which coordinate must grow/shrink
        double sign;
    };
    const Case cases[] = {{0, "z", 1}, {90, "x", -1}, {180, "z", -1}, {270, "x", 1}};
    for (const Case& c : cases) {
        const auto result = mwi::sim::runScenario(parseScenario(tunnelJson(c.yaw, 4)));
        const double moved = std::string(c.axis) == "x" ? result.finalPosition.x - 8.5
                                                        : result.finalPosition.z - 8.5;
        const std::string label = std::string("tunnel yaw ") + std::to_string(c.yaw) + " digs " +
                                  result.direction;
        check(result.status == "completed" && result.direction ==
                  std::string(c.yaw == 0 ? "south" : c.yaw == 90 ? "west" : c.yaw == 180 ? "north" : "east"),
              label);
        check(moved * c.sign > 1.5 && moved * c.sign < 6.0,
              label + ": the player advanced about length blocks along the axis");
    }
    {
        // A tunnel through solid stone has to break the blocks in front of it.
        const auto result = mwi::sim::runScenario(parseScenario(tunnelJson(270, 3)));
        check(result.playerMetrics.obstacleBlocksBroken >= 3 && result.playerMetrics.targetBlocksBroken == 0,
              "digging a corridor breaks obstacle blocks, not targets");
        bool insideSolid = false;
        for (const auto& event : result.breaks) {
            if (event.x == 11 && event.y == 38 && event.z == 8) insideSolid = true;
        }
        check(!result.breaks.empty() && insideSolid,
              "the first corridor block east of the player is broken");
        check(result.playerMetrics.refusedBreaks == 0, "no break was refused in plain stone");
    }
    {
        const auto result = mwi::sim::runScenario(parseScenario(tunnelJson(270, 1)));
        check(result.ticks > 0 && result.ticks <= 1200, "a bounded tunnel stays inside max_ticks");
    }
}

void testDeterminismAndBudget() {
    section("determinism and tick budget");
    const auto json = mineJson("minecraft:coal_ore", 4, 3, 6.0);
    const auto a = mwi::sim::runScenario(parseScenario(json));
    const auto b = mwi::sim::runScenario(parseScenario(json));
    check(a.breaks.size() == b.breaks.size() && a.breaks[0].x == b.breaks[0].x &&
              a.breaks[0].block == b.breaks[0].block,
          "two identical runs break the same block");
    check(a.status == b.status && a.inventory == b.inventory,
          "two identical runs produce the same status and inventory");
    check(a.ticks == b.ticks && a.breaks.size() == b.breaks.size(),
          "two identical runs need the same number of ticks");
    check(a.deterministic && b.deterministic && a.planWaitTimeouts == 0,
          "the runner waited for every background plan, so tick counts are reproducible");
    const auto round = mwi::json::parse(mwi::json::stringify(a.toJson(), false));
    check(round.isObject() && round.get("status") != nullptr,
          "the result serializes to valid JSON");

    {
        auto scenario = parseScenario(mineJson("minecraft:iron_ore", 12, 3, 6.0));
        scenario.maxTicks = 3;
        const auto result = mwi::sim::runScenario(scenario);
        check(result.status == "timeout" && result.ticks <= 3 && !result.finished,
              "a short budget reports timeout, not success");
        check(result.initialTargets == 1, "the target is still counted after a timeout");
    }
}

void testResultSchema() {
    section("result JSON schema");
    const auto result = mwi::sim::runScenario(parseScenario(mineJson("minecraft:iron_ore", 4, 3, 6.0)));
    const std::string text = mwi::json::stringify(result.toJson(), false);
    const auto parsed = mwi::json::parse(text);  // must be valid JSON

    check(parsed.get("schema_version")->asInt() == mwi::sim::kResultSchemaVersion,
          "schema_version is present");
    check(parsed.get("engine_version")->asString() == mwi::sim::kEngineVersion,
          "engine_version is present");
    for (const char* key : {"status", "finished", "bot_active", "mode", "direction", "scenario",
                            "ticks", "player", "targets", "breaks", "inventory", "logs", "counts",
                            "quality", "resources", "determinism", "limitations"}) {
        check(parsed.get(key) != nullptr, std::string("result has ") + key);
    }
    check(parsed.get("limitations")->asArray().size() >= 3,
          "limitations are listed instead of hidden");
    const auto& resources = *parsed.get("resources");
    check(resources.get("wall_ms")->asDouble() >= 0 && resources.get("cpu_ms")->asDouble() >= 0 &&
              resources.get("peak_rss_kb")->asDouble() > 0,
          "resource metrics are present and non-negative");
    check(resources.get("database_bytes")->asDouble() == 0 &&
              resources.get("world_snapshot_estimate_bytes")->asDouble() > 0,
          "database is explicitly zero; the snapshot estimate is not");
    check(parsed.get("determinism") != nullptr &&
              parsed.get("determinism")->get("deterministic") != nullptr,
          "the result states whether the run was deterministic");
    check(parsed.get("quality")->get("status_is_auditable")->asBool(),
          "quality reports whether the status is auditable");
    check(text.find("NaN") == std::string::npos && text.find("nan") == std::string::npos,
          "no NaN leaks into the JSON");
}

void testInvalidInputDoesNotLie() {
    section("invalid input");
    check(throws([] { mwi::sim::runScenario(parseScenario(R"({"mode": "tunnel", "length": 300})")); }),
          "an out-of-range tunnel never reaches the runner");
    check(throws([] { parseScenario(R"({"mode": "mine", "targets": []})"); }, "targets"),
          "an empty target list is rejected");
    check(throws([] { parseScenario(R"({"mode": "mine", "targets": ["stone"], "blocks": [{"x": 1, "y": 999, "z": 1, "name": "stone"}]})"); },
                 "y"),
          "a block outside the world is rejected");
}

}  // namespace

int main() {
    std::cout << "sim_harness: headless engine regressions\n";
    try {
        testJson();
        testScenarioSchema();
        testDirectionConvention();
        testMiningHappyPath();
        testToolTiers();
        testDimensionRules();
        testNoTargetAndBlocked();
        testQuantityAndInventory();
        testTunnel();
        testDeterminismAndBudget();
        testResultSchema();
        testInvalidInputDoesNotLie();
    } catch (const std::exception& error) {
        std::cout << "  [fail] unexpected exception: " << error.what() << "\n";
        ++gFailures;
    }

    std::cout << "\n" << (gChecks - gFailures) << "/" << gChecks << " checks passed\n";
    if (gFailures != 0) {
        std::cout << gFailures << " FAILED\n";
        return 1;
    }
    std::cout << "ALL CHECKS PASSED\n";
    return 0;
}
