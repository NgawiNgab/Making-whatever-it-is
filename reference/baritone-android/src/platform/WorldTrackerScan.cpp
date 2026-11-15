#include "platform/WorldTrackerScan.h"

#include "platform/BlockClassifier.h"

namespace baritone::platform {

baritone::StateId ScanStateCache::idOf(const void* block) {
    if (!block) return baritone::kAir;
    if (!reader_) return baritone::kAir;

    ++misses_;
    std::string name = reader_(block);
    if (name.empty()) return baritone::kAir;

    const auto found = byName_.find(name);
    if (found != byName_.end()) return found->second;

    ++names_;
    const baritone::StateId id =
        baritone::BlockRegistry::get().add(classifyBlock(name, nullptr));
    byName_.emplace(std::move(name), id);
    return id;
}

std::size_t scanColumn(ScanStateCache& cache, int baseX, int baseZ, int lx, int lz, int minY,
                       int maxY, const std::function<const void*(int x, int y, int z)>& getBlock,
                       uint16_t* out) {
    std::size_t unreadable = 0;
    const int x = baseX | lx;
    const int z = baseZ | lz;
    for (int y = minY; y < maxY; ++y) {
        const void* block = getBlock(x, y, z);
        const baritone::StateId id = cache.idOf(block);
        if (id == baritone::kAir && block != nullptr) ++unreadable;
        out[((y & 15) << 8) | (lz << 4) | lx] = id;
    }
    return unreadable;
}

}  // namespace baritone::platform
