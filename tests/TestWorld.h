#pragma once
// Test-only world: builds a flat WorldSnapshot with named blocks in it.

#include <memory>
#include <string>

#include "baritone/world/BlockProps.h"
#include "baritone/world/WorldSnapshot.h"
#include "platform/BlockClassifier.h"

namespace baritone_test {

inline baritone::StateId stateFor(const std::string& name) {
    return baritone::BlockRegistry::get().add(baritone::platform::classifyBlock(name, nullptr));
}

// A world of solid stone up to groundY-1, air above.
inline std::shared_ptr<baritone::WorldSnapshot> flatWorld(int minY, int maxY, int groundY) {
    auto world = std::make_shared<baritone::WorldSnapshot>(0, minY, maxY);
    const baritone::StateId stone = stateFor("minecraft:stone");

    for (int cx = -2; cx <= 2; ++cx) {
        for (int cz = -2; cz <= 2; ++cz) {
            auto chunk = world->mutableChunk(cx, cz);
            if (!chunk) {
                chunk = std::make_shared<baritone::ChunkData>(cx, cz, minY, maxY - minY);
            }
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int y = minY; y < groundY; ++y) chunk->set(lx, y, lz, stone);
                }
            }
            world->put(chunk);
        }
    }
    return world;
}

inline void place(const std::shared_ptr<baritone::WorldSnapshot>& world,
                  const baritone::BlockPos& pos, const std::string& name) {
    auto chunk = world->mutableChunk(pos.x >> 4, pos.z >> 4);
    if (!chunk) return;
    chunk->set(pos.x & 15, pos.y, pos.z & 15, stateFor(name));
}

inline std::string blockName(const std::shared_ptr<baritone::WorldSnapshot>& world,
                             const baritone::BlockPos& pos) {
    auto chunk = world->chunk(pos.x >> 4, pos.z >> 4);
    if (!chunk) return "<no chunk>";
    return baritone::BlockRegistry::get().props(chunk->get(pos.x & 15, pos.y, pos.z & 15)).name;
}

}  // namespace baritone_test
