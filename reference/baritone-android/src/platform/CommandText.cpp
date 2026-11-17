#include "platform/CommandText.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace baritone::platform::parsing {
namespace {

bool allDigits(std::string_view text) {
    return !text.empty() && text.size() <= 9 &&
           std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

std::string normalize(std::string name) {
    std::ranges::transform(name, name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    // The mod menu accepts the same shorthand the chat command does: "iron_ore"
    // means "minecraft:iron_ore".
    if (!name.empty() && name.find(':') == std::string::npos) name = "minecraft:" + name;
    return name;
}

}  // namespace

MineRequest parseMineRequest(std::string_view text, int fallbackQuantity) {
    // Commas, semicolons and brackets all act as separators: the menu joins the
    // multi-select list with commas, and people paste "iron_ore,diamond_ore".
    std::string spaced;
    spaced.reserve(text.size());
    for (char c : text) {
        const bool separator = c == ',' || c == ';' || c == '(' || c == ')' || c == '[' || c == ']';
        spaced.push_back(separator ? ' ' : c);
    }

    std::vector<std::string> tokens;
    std::istringstream stream(spaced);
    for (std::string token; stream >> token;) tokens.push_back(std::move(token));

    MineRequest request;
    request.quantity = std::max(fallbackQuantity, 0);
    if (tokens.empty()) {
        request.error = "no block name given";
        return request;
    }

    std::size_t first = 0;
    if (allDigits(tokens.front())) {
        request.quantity = std::stoi(tokens.front());
        first = 1;
        if (first >= tokens.size()) {
            request.error = "a quantity was given but no block name";
            return request;
        }
    }

    for (std::size_t i = first; i < tokens.size(); ++i) {
        std::string name = normalize(tokens[i]);
        // An empty token after normalization is not a name; skip rather than queue a
        // request that can never match.
        if (name == "minecraft:") continue;
        request.names.push_back(std::move(name));
    }
    if (request.names.empty()) request.error = "no usable block name given";
    return request;
}

TunnelRequest parseTunnelRequest(std::string_view text) {
    TunnelRequest request;
    std::istringstream stream{std::string(text)};
    for (std::string token; stream >> token;) {
        std::ranges::transform(token, token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (token == "north" || token == "south" || token == "east" || token == "west") {
            if (request.direction.empty()) request.direction = token;
            else if (request.direction != token) request.error = "two different directions given";
            continue;
        }
        if (allDigits(token) && token.size() <= 6) request.length = std::stoi(token);
        // Anything else is a note from the menu, not a parameter.
    }
    return request;
}

}  // namespace baritone::platform::parsing
