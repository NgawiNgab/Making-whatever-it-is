#include "platform/WorldTracker.h"

#include <algorithm>
#include <chrono>
#include <mutex>

#include "baritone/Settings.h"
#include "platform/BlockClassifier.h"
#include "platform/Log.h"
#include "platform/Paths.h"
#include "sdk/Game.h"

namespace baritone::platform {

using baritone::BlockPos;
using baritone::ChunkData;
using baritone::StateId;
using baritone::WorldSnapshot;
using Clock = std::chrono::steady_clock;

namespace {

std::mutex g_snapshotMutex;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

WorldTracker& WorldTracker::get() {
    static WorldTracker instance;
    return instance;
}

std::shared_ptr<const WorldSnapshot> WorldTracker::snapshot() const {
    std::lock_guard lock(g_snapshotMutex);
    return snapshot_;
}

void WorldTracker::reset() {
    flushCache();
    world_.clear();
    std::lock_guard lock(g_snapshotMutex);
    snapshot_.reset();
    scannedAt_.clear();
    pending_.reset();
    dimension_ = -1;
    region_ = nullptr;
    // Block names do not change between worlds, so the name cache is kept.
}

void WorldTracker::setWorld(const std::string& folder) {
    if (folder == world_) return;
    flushCache();
    world_ = folder;
}

StateId WorldTracker::idOfName(const std::string& name) {
    auto it = namedIds_.find(name);
    if (it != namedIds_.end()) return it->second;
    StateId id = baritone::BlockRegistry::get().add(classifyBlock(name, nullptr));
    namedIds_.emplace(name, id);
    return id;
}

void WorldTracker::ensureCache() {
    bool wanted = baritone::Settings::get().chunkCaching && !world_.empty() && snapshot_;
    if (!wanted) {
        if (cache_) flushCache();
        return;
    }
    if (cache_) return;
    const char* solid =
        dimension_ == 1 ? "minecraft:netherrack" : dimension_ == 2 ? "minecraft:end_stone" : "minecraft:stone";
    std::array<StateId, 4> ids{baritone::kAir, idOfName("minecraft:water"), idOfName("minecraft:lava"),
                               idOfName(solid)};
    auto dir = paths::dataDir() / "cache" / world_ / ("dim" + std::to_string(dimension_));
    cache_ = std::make_shared<baritone::CachedWorld>(dir, minY_, maxY_ - minY_, ids);
    snapshot_->setCache(cache_);
    logger::info("Long-distance cache: {}", dir.string());
}

void WorldTracker::packIntoCache(const ChunkData& chunk) {
    if (cache_) cache_->put(baritone::CachedChunk::pack(chunk, minY_, maxY_ - minY_, nowMs()));
}

void WorldTracker::queueForPacking(std::shared_ptr<const ChunkData> chunk) {
    if (cache_) packQueue_.push(std::move(chunk));
}

std::size_t WorldTracker::repack() {
    if (!cache_ || !snapshot_) return 0;
    while (auto chunk = packQueue_.take()) packIntoCache(*chunk);
    std::size_t n = 0;
    for (auto [cx, cz] : snapshot_->loadedChunks()) {
        if (auto c = snapshot_->chunk(cx, cz)) {
            packIntoCache(*c);
            ++n;
        }
    }
    return n;
}

void WorldTracker::flushCache() {
    if (!cache_) {
        packQueue_.clear();
        return;
    }
    while (auto chunk = packQueue_.take()) packIntoCache(*chunk);
    auto start = Clock::now();
    if (snapshot_) {
        for (auto [cx, cz] : snapshot_->loadedChunks())
            if (auto c = snapshot_->chunk(cx, cz)) packIntoCache(*c);
        snapshot_->setCache(nullptr);
    }
    cache_->saveNow();
    logger::info("Long-distance cache saved ({} chunks in memory, {:.0f} ms)", cache_->cachedChunks(),
                 std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    cache_.reset();
}

void WorldTracker::setDimension(int dimension, int minY, int maxY) {
    flushCache();
    std::lock_guard lock(g_snapshotMutex);
    dimension_ = dimension;
    minY_ = minY;
    maxY_ = maxY;
    snapshot_ = std::make_shared<WorldSnapshot>(dimension, minY, maxY);
    scannedAt_.clear();
    pending_.reset();
    logger::info("World tracker: dimension {} (y {}..{})", dimension, minY, maxY);
}

StateId WorldTracker::idOf(const void* block) {
    if (!block) return baritone::kAir;

    // Keyed by block *name*, not by the returned pointer. BlockSource::getBlock can
    // return the same scratch Block* for different blocks, so a pointer-keyed cache
    // reports mined blocks as still being there (see WorldTrackerScan.h).
    if (!scanCache_) {
        scanCache_ = std::make_unique<ScanStateCache>();
        scanCache_->setReader([](const void* raw) {
            sdk::Block view(raw);
            std::string name = view.name();
            if (name.empty()) name = view.legacyName();
            return name;
        });
    }
    const StateId id = scanCache_->idOf(block);
    if (id == baritone::kAir) {
        if (unknownBlocks_++ < 8)
            logger::warn("unreadable block name at {} (name path may be wrong)", block);
    }
    return id;
}

bool WorldTracker::continueScan(PendingScan& scan, Clock::time_point deadline) {
    const auto sliceStart = Clock::now();
    const sdk::BlockSource source(region_);
    sdk::BlockPos pos;
    bool done = true;
    for (int c = scan.nextColumn; c < 256; ++c) {
        // 4 columns (~1.5k lookups) between clock reads keeps the overrun tiny
        if ((c & 3) == 0 && c != scan.nextColumn && Clock::now() >= deadline) {
            scan.nextColumn = c;
            done = false;
            break;
        }
        const int lx = c >> 4, lz = c & 15;
        pos.x = (scan.cx << 4) | lx;
        pos.z = (scan.cz << 4) | lz;
        for (int y = minY_; y < maxY_; ++y) {
            pos.y = y;
            // No "same pointer means same block" shortcut: it is what made mined
            // blocks linger in the snapshot. idOf() caches by name instead.
            scan.chunk->set(lx, y, lz, idOf(source.getBlock(pos)));
        }
    }
    scan.elapsedMs += std::chrono::duration<double, std::milli>(Clock::now() - sliceStart).count();
    if (done) scan.nextColumn = 256;
    return done;
}

void WorldTracker::finishScan(PendingScan& scan) {
    // a refresh catches block changes: Java repackOnAnyBlockChange repacks the chunk then
    // (chunks leaving the radius are always packed)
    if (!scan.refresh || baritone::Settings::get().repackOnAnyBlockChange) queueForPacking(scan.chunk);
    snapshot_->put(std::move(scan.chunk));
    scannedAt_[WorldSnapshot::key(scan.cx, scan.cz)] = tick_;
    lastChunkScanMs_ = scan.elapsedMs;
}

void WorldTracker::tick(void* region, const baritone::Vec3d& feetPos, int dimension, int minY,
                        int maxY) {
    ++tick_;
    region_ = region;
    if (!snapshot_ || dimension != dimension_ || minY != minY_ || maxY != maxY_)
        setDimension(dimension, minY, maxY);
    ensureCache();
    packQueue_.setLimit(static_cast<std::size_t>(std::max(0, baritone::Settings::get().chunkPackerQueueMaxSize)));

    sdk::BlockPos feet = sdk::BlockPos::floor(
        {static_cast<float>(feetPos.x), static_cast<float>(feetPos.y), static_cast<float>(feetPos.z)});
    int pcx = feet.x >> 4, pcz = feet.z >> 4;
    auto start = Clock::now();
    auto elapsedMs = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };

    // An unfinished scan from an earlier tick goes first; drop it if the player moved away.
    // Joining a world / teleporting: nothing under the player yet, so catch up faster once.
    const bool playerChunkReady = scannedAt_.contains(WorldSnapshot::key(pcx, pcz));
    const double sliceMs = playerChunkReady ? budgetMs : std::max(budgetMs, 10.0);
    const auto deadline = start + std::chrono::duration_cast<Clock::duration>(
                                      std::chrono::duration<double, std::milli>(sliceMs));
    if (pending_) {
        if (std::max(std::abs(pending_->cx - pcx), std::abs(pending_->cz - pcz)) > loadRadius + 1) {
            pending_.reset();
        } else if (continueScan(*pending_, deadline)) {
            finishScan(*pending_);
            pending_.reset();
        }
    }

    // Candidates: missing chunks first, then stale ones near the player, closest first.
    if (!pending_) {
        struct Candidate {
            int cx, cz, dist;
            bool missing;
        };
        std::vector<Candidate> candidates;
        for (int dx = -loadRadius; dx <= loadRadius; ++dx) {
            for (int dz = -loadRadius; dz <= loadRadius; ++dz) {
                int cx = pcx + dx, cz = pcz + dz;
                auto it = scannedAt_.find(WorldSnapshot::key(cx, cz));
                bool missing = it == scannedAt_.end();
                int dist = std::max(std::abs(dx), std::abs(dz));
                bool stale = !missing && dist <= refreshRadius &&
                             tick_ - it->second >= static_cast<uint64_t>(refreshTicks);
                if (missing || stale) candidates.push_back({cx, cz, dx * dx + dz * dz, missing});
            }
        }
        std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

        for (const Candidate& c : candidates) {
            if (Clock::now() >= deadline) break;
            PendingScan scan;
            scan.cx = c.cx;
            scan.cz = c.cz;
            scan.refresh = !c.missing;
            scan.chunk = std::make_shared<ChunkData>(c.cx, c.cz, minY_, maxY_ - minY_);
            if (continueScan(scan, deadline)) {
                finishScan(scan);
            } else {
                pending_ = std::move(scan);
                break;
            }
        }
    }

    // Packing can't be sliced: one job per tick, only while there's budget left, but at
    // least one every 4th tick so a busy scanner can't starve it.
    if (packQueue_.size() && (elapsedMs() < sliceMs || (tick_ & 3) == 0)) {
        if (auto chunk = packQueue_.take()) packIntoCache(*chunk);
    }

    // write the cache regularly, keep only the regions around the player in memory
    if (cache_ && tick_ % 1200 == 0) {
        if (baritone::Settings::get().pruneRegionsFromRAM) cache_->prune(pcx, pcz, 1);
        cache_->saveAsync();
    }

    // forget chunks far outside the radius
    if (tick_ % 100 == 0) {
        for (auto [cx, cz] : snapshot_->loadedChunks()) {
            if (std::max(std::abs(cx - pcx), std::abs(cz - pcz)) > loadRadius + 2) {
                if (auto c = snapshot_->chunk(cx, cz)) queueForPacking(c);
                snapshot_->remove(cx, cz);
                scannedAt_.erase(WorldSnapshot::key(cx, cz));
            }
        }
    }
}

void WorldTracker::refreshAround(const BlockPos& c, int radius) {
    if (!region_ || !snapshot_) return;
    sdk::BlockPos pos;
    for (int x = c.x - radius; x <= c.x + radius; ++x) {
        for (int z = c.z - radius; z <= c.z + radius; ++z) {
            for (int y = std::max(minY_, c.y - radius); y <= std::min(maxY_ - 1, c.y + radius); ++y) {
                pos = {x, y, z};
                snapshot_->setBlock(x, y, z, idOf(sdk::BlockSource(region_).getBlock(pos)));
            }
        }
    }
}

}  // namespace baritone::platform
