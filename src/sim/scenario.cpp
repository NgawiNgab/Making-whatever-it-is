#include "sim/scenario.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace mwi::sim {
namespace {

std::string trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return std::string(text);
}

int optInt(const json::Value& object, std::string_view key, int fallback) {
    const json::Value* value = object.get(key);
    return value ? value->asInt() : fallback;
}

double optDouble(const json::Value& object, std::string_view key, double fallback) {
    const json::Value* value = object.get(key);
    return value ? value->asDouble() : fallback;
}

bool optBool(const json::Value& object, std::string_view key, bool fallback) {
    const json::Value* value = object.get(key);
    return value ? value->asBool() : fallback;
}

std::string optString(const json::Value& object, std::string_view key, std::string fallback) {
    const json::Value* value = object.get(key);
    return value ? value->asString() : fallback;
}

void requireRange(int value, int low, int high, const std::string& what) {
    if (value < low || value > high) {
        throw std::runtime_error(what + " must be between " + std::to_string(low) + " and " +
                                 std::to_string(high));
    }
}

void requireFinite(double value, const std::string& what) {
    if (!std::isfinite(value) || std::abs(value) > 1'000'000.0) {
        throw std::runtime_error(what + " is not a finite, in-range coordinate");
    }
}

std::vector<std::string> parseTargets(const json::Value& root) {
    const json::Value& array = root.require("targets");
    std::vector<std::string> result;
    for (const json::Value& item : array.asArray()) {
        std::string name = normalizeBlockName(item.asString());
        if (name.find_first_of(" \t\r\n[]=") != std::string::npos) {
            throw std::runtime_error("target block selectors with states are not supported: " + name);
        }
        result.push_back(std::move(name));
    }
    return result;
}

}  // namespace

std::string normalizeBlockName(std::string name) {
    name = trim(name);
    std::ranges::transform(name, name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (name.empty()) throw std::runtime_error("block name is empty");
    if (name.find(':') == std::string::npos) name = "minecraft:" + name;
    return name;
}

const char* Scenario::modeName() const { return mode == Mode::Mine ? "mine" : "tunnel"; }

Scenario Scenario::fromJson(const json::Value& root) {
    if (!root.isObject()) throw std::runtime_error("scenario root must be a JSON object");

    Scenario s;
    const std::string mode = root.require("mode").asString();
    if (mode == "mine") s.mode = Mode::Mine;
    else if (mode == "tunnel") s.mode = Mode::Tunnel;
    else throw std::runtime_error("mode must be 'mine' or 'tunnel'");

    s.name = optString(root, "name", "scenario");
    s.dimension = optInt(root, "dimension", 0);
    requireRange(s.dimension, 0, 2, "dimension");
    s.minY = optInt(root, "min_y", -64);
    s.maxY = optInt(root, "max_y", 320);
    s.groundY = optInt(root, "ground_y", -60);
    s.chunkRadius = optInt(root, "chunk_radius", 2);
    if (s.minY % 16 != 0) throw std::runtime_error("min_y must be a multiple of 16 (chunk section layout)");
    if (s.maxY <= s.minY) throw std::runtime_error("max_y must be greater than min_y");
    if (s.groundY < s.minY || s.groundY >= s.maxY) {
        throw std::runtime_error("ground_y must be inside [min_y, max_y)");
    }
    requireRange(s.chunkRadius, 0, 8, "chunk_radius");

    s.player = {8.5, static_cast<double>(s.groundY), 8.5};
    if (const json::Value* player = root.get("player")) {
        if (!player->isObject()) throw std::runtime_error("player must be an object");
        s.player.x = optDouble(*player, "x", s.player.x);
        s.player.y = optDouble(*player, "y", s.player.y);
        s.player.z = optDouble(*player, "z", s.player.z);
        s.yaw = optDouble(*player, "yaw", 0.0);
        s.pitch = optDouble(*player, "pitch", 0.0);
        s.reach = optDouble(*player, "reach", 4.5);
    }
    requireFinite(s.player.x, "player.x");
    requireFinite(s.player.y, "player.y");
    requireFinite(s.player.z, "player.z");
    requireFinite(s.yaw, "player.yaw");
    requireFinite(s.pitch, "player.pitch");
    if (!std::isfinite(s.reach) || s.reach <= 0.0 || s.reach > 16.0) {
        throw std::runtime_error("player.reach must be in (0, 16]");
    }

    if (const json::Value* hotbar = root.get("hotbar")) {
        for (const json::Value& item : hotbar->asArray()) {
            if (!item.isObject()) throw std::runtime_error("hotbar entries must be objects");
            HotbarItem stack;
            stack.name = normalizeBlockName(item.require("name").asString());
            stack.count = optInt(item, "count", 1);
            stack.tool = optString(item, "tool", "none");
            stack.toolTier = optInt(item, "tier", 0);
            stack.toolSpeed = optDouble(item, "speed", 1.0);
            stack.maxDamage = optInt(item, "max_damage", 0);
            requireRange(stack.count, 1, 64, "hotbar count");
            requireRange(stack.toolTier, 0, 5, "hotbar tier");
            if (!std::isfinite(stack.toolSpeed) || stack.toolSpeed <= 0.0) {
                throw std::runtime_error("hotbar speed must be positive");
            }
            if (stack.maxDamage < 0) throw std::runtime_error("hotbar max_damage must not be negative");
            static constexpr const char* kTools[] = {"none", "pickaxe", "axe", "shovel", "hoe", "sword", "shears"};
            if (std::ranges::find(kTools, stack.tool) == std::end(kTools)) {
                throw std::runtime_error("unknown hotbar tool: " + stack.tool);
            }
            s.hotbar.push_back(std::move(stack));
        }
        if (s.hotbar.size() > 36) throw std::runtime_error("hotbar supports at most 36 slots");
    }

    if (const json::Value* inventory = root.get("inventory")) {
        for (const auto& [name, value] : inventory->asObject()) {
            if (!value) throw std::runtime_error("inventory value is null");
            const int count = value->asInt();
            if (count < 0 || count > 1'000'000) throw std::runtime_error("inventory count is out of range");
            s.inventory[normalizeBlockName(name)] = count;
        }
    }

    if (const json::Value* blocks = root.get("blocks")) {
        for (const json::Value& item : blocks->asArray()) {
            if (!item.isObject()) throw std::runtime_error("block entries must be objects");
            BlockPlacement block;
            block.x = item.require("x").asInt();
            block.y = item.require("y").asInt();
            block.z = item.require("z").asInt();
            block.name = normalizeBlockName(item.require("name").asString());
            if (block.y < s.minY || block.y >= s.maxY) {
                throw std::runtime_error("block y is outside [min_y, max_y)");
            }
            if (std::abs(block.x) > 1'000'000 || std::abs(block.z) > 1'000'000) {
                throw std::runtime_error("block coordinate is out of range");
            }
            const bool netherOnly = block.name == "minecraft:ancient_debris" ||
                                    block.name == "minecraft:nether_gold_ore" ||
                                    block.name == "minecraft:nether_quartz_ore" ||
                                    block.name == "minecraft:quartz_ore" ||
                                    block.name == "minecraft:netherrack" ||
                                    block.name == "minecraft:soul_sand" ||
                                    block.name == "minecraft:soul_soil" ||
                                    block.name == "minecraft:magma" ||
                                    block.name == "minecraft:glowstone" ||
                                    block.name == "minecraft:basalt" ||
                                    block.name == "minecraft:blackstone";
            if (netherOnly && s.dimension != 1) {
                throw std::runtime_error(block.name +
                                         " only exists in the nether; set \"dimension\": 1");
            }
            s.blocks.push_back(std::move(block));
        }
    }

    if (const json::Value* targets = root.get("targets")) {
        (void)targets;
        s.targets = parseTargets(root);
    }
    if (s.mode == Mode::Mine && s.targets.empty()) {
        throw std::runtime_error("mine scenarios require at least one entry in targets");
    }
    if (s.mode == Mode::Mine && s.dimension != 1) {
        for (const std::string& target : s.targets) {
            if (target == "minecraft:ancient_debris" || target == "minecraft:nether_gold_ore" ||
                target == "minecraft:nether_quartz_ore" || target == "minecraft:quartz_ore") {
                throw std::runtime_error(target +
                                         " only exists in the nether; set \"dimension\": 1");
            }
        }
    }

    s.quantity = optInt(root, "quantity", 0);
    s.searchRadius = optInt(root, "search_radius", 12);
    s.length = optInt(root, "length", 0);
    s.direction = optString(root, "direction", "");
    s.maxTicks = optInt(root, "max_ticks", 500);
    s.tickDelayMs = optInt(root, "tick_delay_ms", 1);
    s.planBudgetMs = optInt(root, "plan_budget_ms", 3000);
    s.breakTicks = optInt(root, "break_ticks", 1);
    s.explore = optBool(root, "explore", false);
    s.allowPlace = optBool(root, "allow_place", false);
    s.allowParkour = optBool(root, "allow_parkour", false);
    requireRange(s.quantity, 0, 1'000'000, "quantity");
    requireRange(s.searchRadius, 0, 64, "search_radius");
    requireRange(s.length, 0, 256, "length");
    requireRange(s.maxTicks, 1, 100'000, "max_ticks");
    requireRange(s.tickDelayMs, 0, 100, "tick_delay_ms");
    requireRange(s.planBudgetMs, 10, 60'000, "plan_budget_ms");
    requireRange(s.breakTicks, 1, 100, "break_ticks");
    if (!s.direction.empty() && s.direction != "facing" && s.direction != "north" &&
        s.direction != "south" && s.direction != "east" && s.direction != "west") {
        throw std::runtime_error("direction must be north, south, east, west, facing, or empty");
    }
    if (s.mode == Mode::Tunnel && s.length == 0) {
        // A bounded run is still enforced by max_ticks; this is just an explicit note.
        // Validation allows it so continuous-tunnel scenarios remain expressible.
    }
    return s;
}

std::string directionName(const Scenario& scenario) {
    if (!scenario.direction.empty() && scenario.direction != "facing") return scenario.direction;
    // LayerBase stores yaw in (-180, 180]; -90 must mean west, not south.
    double yaw = std::fmod(scenario.yaw, 360.0);
    if (yaw < 0.0) yaw += 360.0;
    if (yaw >= 360.0) yaw -= 360.0;
    // Core yaw convention (Rotation.h): 0 = south/+Z, 90 = west/-X,
    // 180 = north/-Z, 270 = east/+X. Matches Orientation.cpp; the old
    // TunnelProcess::nearestHorizontal had north/south swapped.
    if (yaw >= 315.0 || yaw < 45.0) return "south";
    if (yaw < 135.0) return "west";
    if (yaw < 225.0) return "north";
    return "east";
}

Vec3 directionDelta(const Scenario& scenario) {
    const std::string name = directionName(scenario);
    double dx = 0.0, dz = 0.0;
    if (name == "north") dz = -1.0;
    else if (name == "south") dz = 1.0;
    else if (name == "west") dx = -1.0;
    else if (name == "east") dx = 1.0;
    const double distance = static_cast<double>(scenario.length);
    return {dx * distance, 0.0, dz * distance};
}

json::Value Scenario::summary() const {
    json::Array targetValues;
    for (const std::string& target : this->targets) targetValues.push_back(json::Value::makeString(target));
    return json::object({
        {"name", json::Value::makeString(name)},
        {"dimension", json::Value::makeNumber(dimension)},
        {"mode", json::Value::makeString(modeName())},
        {"min_y", json::Value::makeNumber(minY)},
        {"max_y", json::Value::makeNumber(maxY)},
        {"ground_y", json::Value::makeNumber(groundY)},
        {"chunk_radius", json::Value::makeNumber(chunkRadius)},
        {"player", json::object({
            {"x", json::Value::makeNumber(player.x)},
            {"y", json::Value::makeNumber(player.y)},
            {"z", json::Value::makeNumber(player.z)},
            {"yaw", json::Value::makeNumber(yaw)},
            {"pitch", json::Value::makeNumber(pitch)},
            {"reach", json::Value::makeNumber(reach)},
        })},
        {"hotbar_slots", json::Value::makeNumber(static_cast<double>(hotbar.size()))},
        {"initial_inventory_items", json::Value::makeNumber(static_cast<double>(inventory.size()))},
        {"blocks", json::Value::makeNumber(static_cast<double>(blocks.size()))},
        {"targets", json::Value::makeArray(std::move(targetValues))},
        {"quantity", json::Value::makeNumber(quantity)},
        {"search_radius", json::Value::makeNumber(searchRadius)},
        {"length", json::Value::makeNumber(length)},
        {"direction", json::Value::makeString(direction.empty() ? "facing" : direction)},
        {"max_ticks", json::Value::makeNumber(maxTicks)},
        {"tick_delay_ms", json::Value::makeNumber(tickDelayMs)},
        {"plan_budget_ms", json::Value::makeNumber(planBudgetMs)},
        {"break_ticks", json::Value::makeNumber(breakTicks)},
        {"explore", json::Value::makeBool(explore)},
        {"allow_place", json::Value::makeBool(allowPlace)},
        {"allow_parkour", json::Value::makeBool(allowParkour)},
    });
}

}  // namespace mwi::sim
