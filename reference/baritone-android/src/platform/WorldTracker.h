#pragma once
#include <cstdint>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "baritone/cache/CachedWorld.h"
#include "baritone/cache/ChunkPackerQueue.h"
#include "baritone/utils/BlockPos.h"
#include "baritone/world/WorldSnapshot.h"

namespace sdk {
class BlockSource;
}

namespace baritone::platform {

// Keeps the WorldSnapshot (read by path calculations on another thread) in sync with the
// game world. Game thread only, except snapshot() which any thread may call.
class WorldTracker {
public:
    static WorldTracker& get();

    // Loads/refreshes chunks around the player within a time budget.
    void tick(void* region, const baritone::Vec3d& feet, int dimension, int minY, int maxY);
    // Leaving the world / unloading: writes the long-distance cache and forgets everything.
    void reset();
    // Folder of the current world in the cache directory (chunkCaching). Set before tick().
    void setWorld(const std::string& folder);
    std::shared_ptr<baritone::CachedWorld> cache() const { return cache_; }
    // #repack: re-packs every loaded chunk into the cache; returns how many.
    std::size_t repack();
    // #saveall / #reloadall
    void saveCache() {
        if (cache_) cache_->saveNow();
    }
    void reloadCache() {
        flushCache();
        ensureCache();
    }

    // Block state id of a game block, registering it on first sight.
    baritone::StateId idOf(const void* block);

    // Re-reads the live blocks of a cube into the snapshot (before acting on them).
    void refreshAround(const baritone::BlockPos& center, int radius);
    // #render: every loaded chunk is scanned again over the next ticks
    void markAllStale() { for (auto& [key, tick] : scannedAt_) tick = 0; }

    std::shared_ptr<const baritone::WorldSnapshot> snapshot() const;
    int dimension() const { return dimension_; }

    // Stats for #debug
    std::size_t knownStates() const { return blockIds_.size(); }
    std::size_t scannedChunks() const { return scannedAt_.size(); }
    double lastChunkScanMs() const { return lastChunkScanMs_; }
    std::size_t pendingPacks() const { return packQueue_.size(); }
    bool cacheActive() const { return !world_.empty(); }

    int loadRadius = 2;       // chunks around the player kept in the snapshot
    int refreshRadius = 0;    // chunks re-scanned regularly (catch block changes)
    int refreshTicks = 40;    // how often, in ticks
    double budgetMs = 2.0;    // per tick (scanning is sliced to fit)

private:
    WorldTracker() = default;
    // A chunk scan is ~98k block lookups on the game thread, far more than one tick's
    // budget on a phone. It is done in slices and only published once complete.
    struct PendingScan {
        int cx = 0, cz = 0;
        bool refresh = false;
        std::shared_ptr<baritone::ChunkData> chunk;
        int nextColumn = 0;  // 0..255, lx = c >> 4, lz = c & 15
        double elapsedMs = 0;
    };
    bool continueScan(PendingScan& scan, std::chrono::steady_clock::time_point deadline);
    void finishScan(PendingScan& scan);
    std::optional<PendingScan> pending_;
    void setDimension(int dimension, int minY, int maxY);
    // Long-distance cache of the current dimension: created when needed, written when leaving.
    void ensureCache();
    void flushCache();
    void packIntoCache(const baritone::ChunkData& chunk);
    void queueForPacking(std::shared_ptr<const baritone::ChunkData> chunk);
    baritone::ChunkPackerQueue packQueue_;
    baritone::StateId idOfName(const std::string& name);

    std::string world_;
    std::shared_ptr<baritone::CachedWorld> cache_;
    std::unordered_map<std::string, baritone::StateId> namedIds_;

    void* region_ = nullptr;
    std::shared_ptr<baritone::WorldSnapshot> snapshot_;
    std::unordered_map<const void*, baritone::StateId> blockIds_;
    std::unordered_map<int64_t, uint64_t> scannedAt_;  // chunk key -> tick
    uint64_t tick_ = 0;
    int dimension_ = -1;
    int minY_ = 0, maxY_ = 0;
    double lastChunkScanMs_ = 0;
    std::size_t unknownBlocks_ = 0;
};

}  // namespace baritone::platform
