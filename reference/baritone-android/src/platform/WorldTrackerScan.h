#pragma once
// The chunk scan, split out of WorldTracker so it can be tested without a game.
//
// Why this is separate: the scan used to register each block once and reuse that
// state id for every later block at the same address:
//
//     if (block != lastBlock) { lastBlock = block; lastId = idOf(block); }
//     if (lastId != kAir) chunk->set(lx, y, lz, lastId);
//
// `BlockSource::getBlock` can hand back the same `Block*` for different blocks -
// Bedrock keeps a small pool of scratch Block objects - so "same pointer" does not
// mean "same block". Mined blocks then stayed in the snapshot as the ore they used
// to be, and the bot would path back to a hole forever. Reported as: it mines one
// and then loses track of where the others are.
//
// The fix is to always ask for the name and let a small cache do the work: names
// repeat far more than pointers change, and a hit still costs one hash lookup
// instead of a vtable call.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "baritone/world/BlockProps.h"

namespace baritone::platform {

// Reads the identifier of whatever the game returns for a position.
// Returns an empty string when the block could not be read.
using BlockNameReader = std::function<std::string(const void*)>;

// Caches name -> state id, and counts how often each lookup had to do real work.
class ScanStateCache {
public:
    // State id for a game Block*, registering the name on first sight.
    // `block` is whatever BlockSource::getBlock returned; nullptr is air.
    baritone::StateId idOf(const void* block);

    void setReader(BlockNameReader reader) { reader_ = std::move(reader); }
    void clear() {
        byName_.clear();
        names_ = 0;
        misses_ = 0;
    }

    std::size_t names() const { return names_; }
    std::size_t misses() const { return misses_; }

private:
    BlockNameReader reader_;
    std::unordered_map<std::string, baritone::StateId> byName_;
    std::size_t names_ = 0;   // distinct names seen
    std::size_t misses_ = 0;  // lookups that had to call the reader
};

// One chunk column's worth of blocks, written into `out`.
// `out` is indexed by the same (y << 8) | (lz << 4) | lx layout as ChunkData.
// Returns the number of blocks that could not be read (0 is the happy path).
std::size_t scanColumn(ScanStateCache& cache, int baseX, int baseZ, int lx, int lz, int minY,
                       int maxY, const std::function<const void*(int x, int y, int z)>& getBlock,
                       uint16_t* out);

}  // namespace baritone::platform
