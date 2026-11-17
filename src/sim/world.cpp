#include "sim/world.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/cache/WorldScanner.h"
#include "platform/BlockClassifier.h"

namespace mwi::sim {
namespace {

constexpr std::size_t kMaxChunkColumns = 1024;

std::string dropItemName(std::string_view block) {
    if (block == "minecraft:stone") return "minecraft:cobblestone";
    if (block == "minecraft:deepslate") return "minecraft:cobbled_deepslate";
    if (block == "minecraft:grass_block" || block == "minecraft:mycelium" ||
        block == "minecraft:podzol") return "minecraft:dirt";
    if (block == "minecraft:iron_ore" || block == "minecraft:deepslate_iron_ore") return "minecraft:raw_iron";
    if (block == "minecraft:copper_ore" || block == "minecraft:deepslate_copper_ore") return "minecraft:raw_copper";
    if (block == "minecraft:gold_ore" || block == "minecraft:deepslate_gold_ore") return "minecraft:raw_gold";
    if (block == "minecraft:coal_ore" || block == "minecraft:deepslate_coal_ore") return "minecraft:coal";
    if (block == "minecraft:diamond_ore" || block == "minecraft:deepslate_diamond_ore") return "minecraft:diamond";
    if (block == "minecraft:emerald_ore" || block == "minecraft:deepslate_emerald_ore") return "minecraft:emerald";
    if (block == "minecraft:lapis_ore" || block == "minecraft:deepslate_lapis_ore") return "minecraft:lapis_lazuli";
    if (block == "minecraft:redstone_ore" || block == "minecraft:lit_redstone_ore" ||
        block == "minecraft:deepslate_redstone_ore" || block == "minecraft:lit_deepslate_redstone_ore")
        return "minecraft:redstone";
    if (block == "minecraft:quartz_ore" || block == "minecraft:nether_quartz_ore") return "minecraft:quartz";
    if (block == "minecraft:nether_gold_ore") return "minecraft:gold_nugget";
    if (block == "minecraft:glowstone") return "minecraft:glowstone_dust";
    if (block == "minecraft:snow") return "minecraft:snowball";
    if (block == "minecraft:clay") return "minecraft:clay_ball";
    if (block == "minecraft:melon_block") return "minecraft:melon_slice";
    if (block == "minecraft:sea_lantern") return "minecraft:prismarine_crystals";
    if (block == "minecraft:amethyst_cluster") return "minecraft:amethyst_shard";
    return std::string(block);
}

// PlayerContext tier scale: 1 wood/gold, 2 stone, 3 iron, 4 diamond, 5 netherite.
const char* toolTierName(int tier) {
    switch (tier) {
        case 1: return "wooden";
        case 2: return "stone";
        case 3: return "iron";
        case 4: return "diamond";
        case 5: return "netherite";
        default: return "hand";
    }
}

std::string toolName(baritone::Tool tool) {
    switch (tool) {
        case baritone::Tool::Pickaxe: return "pickaxe";
        case baritone::Tool::Axe: return "axe";
        case baritone::Tool::Shovel: return "shovel";
        case baritone::Tool::Hoe: return "hoe";
        case baritone::Tool::Sword: return "sword";
        case baritone::Tool::Shears: return "shears";
        case baritone::Tool::None: return "none";
    }
    return "none";
}

}  // namespace

World::World(const Scenario& scenario) : scenario_(scenario) {
    snapshot_ = std::make_shared<baritone::WorldSnapshot>(0, scenario.minY, scenario.maxY);

    // Bounds include the player, every declared block, and the tunnel endpoint,
    // plus a 16-block margin and the requested chunk radius.
    const Vec3 delta = directionDelta(scenario);
    double minX = std::min(scenario.player.x, scenario.player.x + delta.x) - 16.0;
    double maxX = std::max(scenario.player.x, scenario.player.x + delta.x) + 16.0;
    double minZ = std::min(scenario.player.z, scenario.player.z + delta.z) - 16.0;
    double maxZ = std::max(scenario.player.z, scenario.player.z + delta.z) + 16.0;
    for (const BlockPlacement& block : scenario.blocks) {
        minX = std::min(minX, static_cast<double>(block.x) - 16.0);
        maxX = std::max(maxX, static_cast<double>(block.x) + 16.0);
        minZ = std::min(minZ, static_cast<double>(block.z) - 16.0);
        maxZ = std::max(maxZ, static_cast<double>(block.z) + 16.0);
    }
    const int minCx = static_cast<int>(std::floor(minX / 16.0)) - scenario.chunkRadius;
    const int maxCx = static_cast<int>(std::floor(maxX / 16.0)) + scenario.chunkRadius;
    const int minCz = static_cast<int>(std::floor(minZ / 16.0)) - scenario.chunkRadius;
    const int maxCz = static_cast<int>(std::floor(maxZ / 16.0)) + scenario.chunkRadius;
    const auto columns = static_cast<std::size_t>(maxCx - minCx + 1) *
                         static_cast<std::size_t>(maxCz - minCz + 1);
    if (columns > kMaxChunkColumns) {
        throw std::runtime_error("scenario would create " + std::to_string(columns) +
                                 " chunk columns (limit " + std::to_string(kMaxChunkColumns) + ")");
    }

    // Two grids, because a tunnel needs a world with something to dig:
    //   mine   - a flat stone floor up to ground_y, open air above (the player walks
    //            on the surface, as in a "find this ore" scenario).
    //   tunnel - a solid rock mass from min_y through ground_y, so the player starts
    //            buried at ground_y and the corridor has to be carved. Generating air
    //            for a tunnel made a "tunnel" run walk through an empty world.
    const baritone::StateId stone = stateFor("minecraft:stone");
    const bool buried = scenario.mode == Mode::Tunnel;
    for (int cx = minCx; cx <= maxCx; ++cx) {
        for (int cz = minCz; cz <= maxCz; ++cz) {
            auto chunk = std::make_shared<baritone::ChunkData>(cx, cz, scenario.minY,
                                                               scenario.maxY - scenario.minY);
            const int solidTop = buried ? scenario.groundY + 1 : scenario.groundY;
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int y = scenario.minY; y < solidTop; ++y) {
                        chunk->set(lx, y, lz, stone);
                    }
                }
            }
            snapshot_->put(std::move(chunk));
        }
    }

    // A buried start needs a pocket to stand in, exactly like a player who dug one
    // block down. Without it the player's own feet block is solid, every candidate
    // step collides, and the bot can never move (it dug three blocks then stalled).
    if (buried) {
        const int px = static_cast<int>(std::floor(scenario.player.x));
        const int py = static_cast<int>(std::floor(scenario.player.y));
        const int pz = static_cast<int>(std::floor(scenario.player.z));
        const auto chunk = snapshot_->mutableChunk(px >> 4, pz >> 4);
        if (chunk) {
            chunk->set(px & 15, py, pz & 15, baritone::kAir);
            chunk->set(px & 15, py + 1, pz & 15, baritone::kAir);
        }
    }

    std::set<baritone::BlockPos> seen;
    for (const BlockPlacement& block : scenario.blocks) {
        const int cx = block.x >> 4, cz = block.z >> 4;
        auto chunk = snapshot_->mutableChunk(cx, cz);
        if (!chunk) throw std::runtime_error("block lies outside generated chunks");
        chunk->set(block.x & 15, block.y, block.z & 15, stateFor(block.name));
        baritone::BlockPos pos{block.x, block.y, block.z};
        if (seen.insert(pos).second) placedPositions_.push_back(pos);
    }
}

baritone::StateId World::stateFor(const std::string& name) {
    auto it = states_.find(name);
    if (it != states_.end()) return it->second;
    baritone::BlockProps props = baritone::platform::classifyBlock(name, nullptr);
    const baritone::StateId id = baritone::BlockRegistry::get().add(std::move(props));
    states_.emplace(name, id);
    return id;
}

const baritone::BlockProps& World::props(const baritone::BlockPos& pos) const {
    auto chunk = snapshot_->chunk(pos.x >> 4, pos.z >> 4);
    const baritone::StateId id = chunk ? chunk->get(pos.x & 15, pos.y, pos.z & 15) : baritone::kAir;
    return baritone::BlockRegistry::get().props(id);
}

std::string World::blockName(const baritone::BlockPos& pos) const { return props(pos).name; }

bool World::isSolid(int x, int y, int z) const {
    const baritone::BlockProps& p = props({x, y, z});
    return p.type != baritone::BlockType::Air && p.collides && !p.hasFluid();
}

bool World::isTarget(const baritone::BlockPos& pos, const baritone::BlockFilter& filter) const {
    return filter.matches(props(pos));
}

void World::setAir(const baritone::BlockPos& pos) {
    snapshot_->setBlock(pos.x, pos.y, pos.z, baritone::kAir);
}

DropDecision World::dropFor(const baritone::BlockPos& pos,
                            const baritone::HotbarStack& selected) const {
    const baritone::BlockProps& p = props(pos);
    if (p.name == "minecraft:air") return {false, false, true, "", "air is not breakable"};
    if (p.unbreakable) return {false, false, true, "", "block is marked unbreakable"};

    // Same rule as InventoryBehavior::breakSpeed: never keep a second copy of the
    // harvest-level table here, or the engine reports drops the bot cannot get.
    const int tier = baritone::requiredHarvestTier(p);
    const bool satisfied = !p.requiresTool ||
                           (!selected.empty() && selected.tool == p.bestTool &&
                            selected.toolTier >= tier);
    if (!satisfied) {
        return {true, true, false, "",
                "no drop: needs a " + std::string(toolTierName(tier)) + " " + toolName(p.bestTool) +
                    " (held tier " + std::to_string(selected.toolTier) + ")"};
    }
    return {true, p.requiresTool, true, dropItemName(p.name), {}};
}

void World::collectSearchResults(const baritone::BlockFilter& filter, int searchRadius,
                                std::vector<baritone::BlockPos>& outTargets,
                                std::vector<baritone::BlockPos>& outObstacles) const {
    const baritone::BlockPos center{static_cast<int>(std::floor(scenario_.player.x)),
                                    static_cast<int>(std::floor(scenario_.player.y)),
                                    static_cast<int>(std::floor(scenario_.player.z))};
    const int radius = std::clamp(searchRadius, 0, 128);
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const baritone::BlockPos pos{center.x + dx, center.y + dy, center.z + dz};
                if (pos.y < scenario_.minY || pos.y >= scenario_.maxY) continue;
                const baritone::BlockProps& p = props(pos);
                if (p.name == "minecraft:air" || p.type == baritone::BlockType::Air) continue;
                if (filter.matches(p)) outTargets.push_back(pos);
                else if (isSolid(pos.x, pos.y, pos.z)) outObstacles.push_back(pos);
            }
        }
    }
    // Squared distance from the player, so the closest reachable block wins and
    // ties keep a deterministic scan order.
    const auto distance = [&](const baritone::BlockPos& pos) {
        const double dx = pos.x - center.x + 0.5, dy = pos.y - center.y, dz = pos.z - center.z + 0.5;
        return dx * dx + dy * dy + dz * dz;
    };
    const auto byDistance = [&](const baritone::BlockPos& a, const baritone::BlockPos& b) {
        return distance(a) < distance(b);
    };
    std::stable_sort(outTargets.begin(), outTargets.end(), byDistance);
    std::stable_sort(outObstacles.begin(), outObstacles.end(), byDistance);
}

void World::cacheInitialTargets(const baritone::BlockFilter& filter) {
    initialTargetPositions_.clear();
    for (const baritone::BlockPos& pos : placedPositions_) {
        if (isTarget(pos, filter)) initialTargetPositions_.push_back(pos);
    }
    initialTargetsCached_ = true;
}

std::size_t World::initialTargetCount(const baritone::BlockFilter& filter) const {
    // Runner normally calls cacheInitialTargets before the first tick. The lazy
    // path keeps direct unit tests honest if they forget, but may undercount after
    // blocks have already been broken.
    if (!initialTargetsCached_) const_cast<World*>(this)->cacheInitialTargets(filter);
    return initialTargetPositions_.size();
}

std::size_t World::remainingTargetCount(const baritone::BlockFilter& filter) const {
    std::size_t count = 0;
    for (const baritone::BlockPos& pos : placedPositions_) {
        if (isTarget(pos, filter)) ++count;
    }
    return count;
}

std::size_t World::estimateSnapshotBytes() const {
    std::size_t bytes = sizeof(baritone::WorldSnapshot);
    for (const auto& [cx, cz] : snapshot_->loadedChunks()) {
        auto chunk = snapshot_->chunk(cx, cz);
        if (!chunk) continue;
        bytes += sizeof(baritone::ChunkData) +
                 static_cast<std::size_t>(chunk->numSections()) * sizeof(void*);
        for (int section = 0; section < chunk->numSections(); ++section) {
            if (chunk->section(section)) {
                bytes += sizeof(baritone::Section) + 4096 * sizeof(baritone::StateId);
            }
        }
    }
    return bytes;
}

}  // namespace mwi::sim
