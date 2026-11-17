#pragma once
// Strict scenario schema for the headless simulation CLI.

#include <map>
#include <string>
#include <vector>

#include "sim/json.h"

namespace mwi::sim {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BlockPlacement {
    int x = 0;
    int y = 0;
    int z = 0;
    std::string name;
};

struct HotbarItem {
    std::string name;
    int count = 1;
    std::string tool = "none";
    int toolTier = 0;
    double toolSpeed = 1.0;
    int maxDamage = 0;
};

enum class Mode { Mine, Tunnel };

struct Scenario {
    std::string name = "scenario";
    Mode mode = Mode::Mine;

    int minY = -64;
    int maxY = 320;
    int groundY = -60;
    int chunkRadius = 2;

    Vec3 player{8.5, -60.0, 8.5};
    double yaw = 0.0;
    double pitch = 0.0;
    double reach = 4.5;

    std::vector<HotbarItem> hotbar;
    std::map<std::string, int> inventory;
    std::vector<BlockPlacement> blocks;
    std::vector<std::string> targets;

    int quantity = 0;
    // Blocks searched around the player when deciding "target reachable" vs "blocked".
    // Kept explicit so a mine run has a fixed, auditable amount of work.
    int searchRadius = 12;
    int length = 0;
    std::string direction;  // empty or "facing" = derive from yaw

    int maxTicks = 500;
    int tickDelayMs = 1;
    // How long the runner waits for one background path plan before giving up and
    // marking the run non-deterministic. Generous by default: the wait costs nothing
    // when planning is fast.
    int planBudgetMs = 3000;
    int breakTicks = 1;
    bool explore = false;
    bool allowPlace = false;
    bool allowParkour = false;

    static Scenario fromJson(const json::Value& root);
    json::Value summary() const;
    const char* modeName() const;
};

std::string normalizeBlockName(std::string name);

// World-space displacement covered by a tunnel scenario. An empty direction uses
// the yaw convention of the core: 0 = south/+Z, 90 = west/-X, 180 = north/-Z,
// 270 = east/+X (see baritone/utils/Rotation.h).
Vec3 directionDelta(const Scenario& scenario);
std::string directionName(const Scenario& scenario);

}  // namespace mwi::sim
