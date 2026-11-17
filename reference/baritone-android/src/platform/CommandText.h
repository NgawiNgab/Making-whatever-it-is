#pragma once
// Pure parsers for the text the mod menu hands to BaritoneRuntime.
//
// These used to live inline inside startMineFromText/startTunnelFromText, where the
// only way to test them was to call a method that immediately leaks its private
// queue into a Baritone instance. Extracted so a harness can assert on the exact
// names and quantity that will be used, with no game state involved.

#include <optional>
#include <string>
#include <vector>

namespace baritone::platform::parsing {

struct MineRequest {
    std::vector<std::string> names;  // normalized "namespace:block" names, in order
    int quantity = 0;                // explicit quantity, or the caller's default
    std::string error;               // empty when the request is usable
    bool ok() const { return error.empty(); }
};

// "#mine" argument: an optional leading quantity, then block names separated by
// spaces, commas, semicolons or brackets. `fallbackQuantity` is used when the text
// carries no quantity of its own.
MineRequest parseMineRequest(std::string_view text, int fallbackQuantity);

struct TunnelRequest {
    std::string direction;  // "north"/"south"/"east"/"west", empty = player facing
    int length = 0;         // 0 = as far as it goes
    std::string error;
    bool ok() const { return error.empty(); }
};

// "#tunnel" argument: an optional length and an optional compass direction, in any
// order. Unknown words are ignored (the menu also passes user notes).
TunnelRequest parseTunnelRequest(std::string_view text);

}  // namespace baritone::platform::parsing
