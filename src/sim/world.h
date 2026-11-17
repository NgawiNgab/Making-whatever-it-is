#pragma once
// First-party headless world adapter around the vendored core's WorldSnapshot.

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "baritone/api/PlayerContext.h"
#include "baritone/utils/BlockPos.h"
#include "baritone/world/BlockProps.h"
#include "baritone/world/WorldSnapshot.h"
#include "sim/scenario.h"

namespace baritone {
class BlockFilter;
}

namespace mwi::sim {

struct DropDecision {
    bool breakable = true;
    bool requiresTool = false;
    bool toolSatisfied = true;
    std::string item;
    std::string note;
};

class World {
public:
    explicit World(const Scenario& scenario);

    const std::shared_ptr<baritone::WorldSnapshot>& snapshot() const { return snapshot_; }
    const baritone::BlockProps& props(const baritone::BlockPos& pos) const;
    std::string blockName(const baritone::BlockPos& pos) const;
    bool isSolid(int x, int y, int z) const;
    bool isTarget(const baritone::BlockPos& pos, const baritone::BlockFilter& filter) const;
    void setAir(const baritone::BlockPos& pos);

    // Blocks that can be mined at all, before any reach/path rule. Fills
    // `outTargets` then `outObstacles` with nearest-first positions so the runner can
    // decide determinism: reachable targets now, or blocked by solid blocks.
    void collectSearchResults(const baritone::BlockFilter& filter, int searchRadius,
                              std::vector<baritone::BlockPos>& outTargets,
                              std::vector<baritone::BlockPos>& outObstacles) const;

    DropDecision dropFor(const baritone::BlockPos& pos,
                         const baritone::HotbarStack& selected) const;

    void cacheInitialTargets(const baritone::BlockFilter& filter);
    std::size_t initialTargetCount(const baritone::BlockFilter& filter) const;
    std::size_t remainingTargetCount(const baritone::BlockFilter& filter) const;
    // Approximate in-memory size of the snapshot's chunk sections; not a heap
    // allocator audit and not a persistent database.
    std::size_t estimateSnapshotBytes() const;
    std::size_t loadedChunkCount() const { return snapshot_->chunkCount(); }

private:
    baritone::StateId stateFor(const std::string& name);

    Scenario scenario_;
    std::shared_ptr<baritone::WorldSnapshot> snapshot_;
    std::map<std::string, baritone::StateId> states_;
    std::vector<baritone::BlockPos> placedPositions_;
    std::vector<baritone::BlockPos> initialTargetPositions_;
    bool initialTargetsCached_ = false;
};

}  // namespace mwi::sim
