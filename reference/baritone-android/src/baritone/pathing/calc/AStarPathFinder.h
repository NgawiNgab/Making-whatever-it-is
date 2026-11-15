#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "baritone/pathing/calc/Path.h"
#include "baritone/pathing/calc/PathNode.h"
#include "baritone/pathing/movement/CalculationContext.h"

namespace baritone {

// Cost multipliers for positions (Baritone's Favoring): the previous path is cheaper so the
// bot doesn't flip between equivalent paths; mobs/spawners can be made more expensive.
class Favoring {
public:
    Favoring() = default;
    Favoring(const PathPtr& previous, double backtrackCoefficient);
    void avoidSphere(const BlockPos& center, int radius, double coefficient);
    double calculate(int64_t hash) const {
        if (favorings_.empty()) return 1.0;
        auto it = favorings_.find(hash);
        return it == favorings_.end() ? 1.0 : it->second;
    }
    bool empty() const { return favorings_.empty(); }

private:
    std::unordered_map<int64_t, double> favorings_;
};

// Open-addressing hash map int64 -> PathNode* (linear probing, power-of-two capacity).
// The search does one lookup per generated neighbour; this beats std::unordered_map's
// per-node allocation and pointer chasing.
class NodeMap {
public:
    void reserve(std::size_t n) {
        std::size_t cap = 16;
        while (cap * 7 < n * 10) cap <<= 1;  // keep load <= 0.7
        if (cap > slots_.size()) rehash(cap);
    }
    // Returns the slot's node pointer, inserting nullptr if the key is new.
    PathNode*& findOrInsert(int64_t key, bool& inserted) {
        if (slots_.empty()) rehash(16);
        else if ((size_ + 1) * 10 > slots_.size() * 7) rehash(slots_.size() * 2);
        std::size_t i = index(key);
        while (slots_[i].node && slots_[i].key != key) i = (i + 1) & mask_;
        inserted = slots_[i].node == nullptr;
        slots_[i].key = key;
        if (inserted) {
            ++size_;
            slots_[i].node = kPending;  // claimed; caller overwrites immediately
        }
        return slots_[i].node;
    }

private:
    struct Slot {
        int64_t key = 0;
        PathNode* node = nullptr;
    };
    static inline PathNode* const kPending = reinterpret_cast<PathNode*>(1);
    std::size_t index(int64_t key) const {
        return static_cast<std::size_t>((static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull) >> 20) & mask_;
    }
    void rehash(std::size_t cap) {
        std::vector<Slot> old(cap);
        old.swap(slots_);
        mask_ = cap - 1;
        for (const Slot& s : old) {
            if (!s.node) continue;
            std::size_t i = index(s.key);
            while (slots_[i].node) i = (i + 1) & mask_;
            slots_[i] = s;
        }
    }
    std::vector<Slot> slots_;
    std::size_t mask_ = 0, size_ = 0;
};

enum class PathResultType { SuccessToGoal, SuccessSegment, Failure, Cancellation, Exception };

struct PathCalculationResult {
    PathResultType type;
    PathPtr path;
    std::string error;  // Exception: what went wrong
    int nodes = 0;
};

// A* search (Baritone's AbstractNodeCostSearch + AStarPathFinder). One instance per
// calculation, run on a worker thread; cancel() and the render getters are thread-safe.
class AStarPathFinder {
public:
    AStarPathFinder(BlockPos start, GoalPtr goal, std::unique_ptr<CalculationContext> context, Favoring favoring);
    ~AStarPathFinder();

    PathCalculationResult calculate(long primaryTimeoutMs, long failureTimeoutMs);
    void cancel() { cancelRequested_ = true; }
    bool isFinished() const { return finished_; }

    const GoalPtr& goal() const { return goal_; }
    const BlockPos& start() const { return start_; }

    // For rendering the calculation in progress (copies, thread-safe).
    std::vector<BlockPos> bestPathSoFar() const;
    std::vector<BlockPos> pathToMostRecentNodeConsidered() const;

    static constexpr std::array<double, 7> kCoefficients{1.5, 2, 2.5, 3, 4, 5, 10};
    static constexpr int kMinDistPath = 5;
    static constexpr double kMinImprovement = 0.01;

private:
    PathNode* getNode(int x, int y, int z, int64_t hash);
    std::optional<PathPtr> bestSoFar(int numNodes);
    PathCalculationResult postProcess(PathPtr path, int numNodes);
    double distFromStartSq(const PathNode* n) const;
    static std::vector<BlockPos> walkBack(const PathNode* n);

    BlockPos start_;
    GoalPtr goal_;
    std::unique_ptr<CalculationContext> context_;
    Favoring favoring_;

    // Nodes live in fixed-size blocks so pointers stay valid.
    std::vector<std::unique_ptr<PathNode[]>> blocks_;
    std::size_t blockUsed_ = 0;
    static constexpr std::size_t kBlockSize = 4096;
    NodeMap map_;

    PathNode* startNode_ = nullptr;
    std::array<PathNode*, kCoefficients.size()> bestSoFar_{};
    std::atomic<PathNode*> mostRecentConsidered_{nullptr};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> finished_{false};
};

}  // namespace baritone
