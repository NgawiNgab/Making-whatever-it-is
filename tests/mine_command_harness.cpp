// Tests the shipping #mine / #tunnel text parsers, not just the core.
//
// BaritoneRuntime::startMineFromText is what the mod menu button calls. Its parsing
// used to be untestable (private queue, needs a game), and the old version of this
// harness asserted only "did not crash" - it passed while the parser could return an
// empty block list. The parsing now lives in platform::parsing (CommandText.cpp), so
// every case below asserts the exact names, quantity, direction, and length that the
// bot would receive.

#include <cstdio>
#include <string>

#include "platform/CommandText.h"

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++gFailures;
}

std::string join(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& name : names) {
        if (!out.empty()) out += ',';
        out += name;
    }
    return out;
}

}  // namespace

int main() {
    using namespace baritone::platform::parsing;

    std::printf("\n== #mine argument parsing ==\n");
    {
        const auto r = parseMineRequest("sugarcane", 0);
        check(r.ok() && join(r.names) == "minecraft:sugarcane" && r.quantity == 0,
              "plain name is namespaced, quantity stays 0");
    }
    {
        const auto r = parseMineRequest("iron_ore", 64);
        check(r.ok() && join(r.names) == "minecraft:iron_ore" && r.quantity == 64,
              "caller's quantity is kept when the text has none");
    }
    {
        const auto r = parseMineRequest("64 iron_ore", 0);
        check(r.ok() && join(r.names) == "minecraft:iron_ore" && r.quantity == 64,
              "leading quantity comes from the text");
    }
    {
        const auto r = parseMineRequest("12 64 iron_ore", 3);
        check(r.ok() && r.quantity == 12 && r.names.size() == 2 &&
                  r.names[0] == "minecraft:64",
              "only the first leading number is a quantity (65 is not swallowed)");
    }
    {
        const auto r = parseMineRequest("iron_ore,diamond_ore", 0);
        check(r.ok() && join(r.names) == "minecraft:iron_ore,minecraft:diamond_ore",
              "comma separated names are split and kept in order");
    }
    {
        const auto r = parseMineRequest("minecraft:iron_ore", 0);
        check(r.ok() && join(r.names) == "minecraft:iron_ore",
              "an explicit namespace is not doubled");
    }
    {
        const auto r = parseMineRequest("[iron_ore; gold_ore]", 0);
        check(r.ok() && join(r.names) == "minecraft:iron_ore,minecraft:gold_ore",
              "brackets and semicolons are separators");
    }
    {
        const auto r = parseMineRequest("  IRON_ORE  ", 0);
        check(r.ok() && join(r.names) == "minecraft:iron_ore",
              "whitespace is trimmed and names are lowercased");
    }
    {
        const auto r = parseMineRequest("   ", 5);
        check(!r.ok() && r.names.empty() && !r.error.empty(),
              "blank input is rejected with a reason");
    }
    {
        const auto r = parseMineRequest("64", 0);
        check(!r.ok() && r.names.empty() && r.error.find("no block name") != std::string::npos,
              "a quantity with no block name is rejected");
    }
    {
        const auto r = parseMineRequest("iron_ore", -4);
        check(r.ok() && r.quantity == 0, "a negative quantity is clamped to 0 (keep going)");
    }
    {
        const auto r = parseMineRequest(",;()[]", 0);
        check(!r.ok(), "pure separators are rejected instead of queuing nothing");
    }

    std::printf("\n== #tunnel argument parsing ==\n");
    {
        const auto r = parseTunnelRequest("64");
        check(r.ok() && r.length == 64 && r.direction.empty(),
              "length only, direction stays \"facing\"");
    }
    {
        const auto r = parseTunnelRequest("64 north");
        check(r.ok() && r.length == 64 && r.direction == "north", "length then direction");
    }
    {
        const auto r = parseTunnelRequest("east 12");
        check(r.ok() && r.length == 12 && r.direction == "east",
              "direction then length (order does not matter)");
    }
    {
        const auto r = parseTunnelRequest("NORTH");
        check(r.ok() && r.direction == "north" && r.length == 0,
              "direction is lowercased and length may be 0");
    }
    {
        const auto r = parseTunnelRequest("north south");
        check(!r.ok() && r.error.find("direction") != std::string::npos,
              "two different directions are rejected");
    }
    {
        const auto r = parseTunnelRequest("north north");
        check(r.ok() && r.direction == "north", "the same direction twice is fine");
    }
    {
        const auto r = parseTunnelRequest("just a note 8");
        check(r.ok() && r.length == 8 && r.direction.empty(),
              "unknown words are ignored, a number is still the length");
    }
    {
        const auto r = parseTunnelRequest("999999");
        check(r.ok() && r.length == 999999,
              "a six digit length is accepted (the scenario layer bounds it)");
    }

    std::printf("\n== result ==\n%s\n",
                gFailures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return gFailures == 0 ? 0 : 1;
}
