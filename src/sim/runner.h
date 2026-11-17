#pragma once
// Orchestrates one bounded headless simulation and serializes an auditable result.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "sim/json.h"
#include "sim/player.h"
#include "sim/scenario.h"

namespace mwi::sim {

inline constexpr const char* kEngineVersion = "1.0.0";
inline constexpr int kResultSchemaVersion = 1;

struct LogLine {
    std::string message;
    bool error = false;
};

struct RunResult {
    Scenario scenario;
    std::string status = "error";
    bool finished = false;
    bool botActive = false;
    int ticks = 0;
    std::string direction;

    baritone::Vec3d finalPosition{};
    double finalYaw = 0.0;
    double finalPitch = 0.0;
    int selectedSlot = 0;

    std::vector<BreakEvent> breaks;
    std::map<std::string, int> inventory;
    std::vector<LogLine> logs;
    PlayerMetrics playerMetrics{};

    std::size_t initialTargets = 0;
    std::size_t remainingTargets = 0;
    std::size_t worldSnapshotBytes = 0;
    std::size_t loadedChunks = 0;
    std::uint64_t databaseBytes = 0;  // this engine has no persistent database

    double wallMs = 0.0;
    double cpuMs = 0.0;
    std::uint64_t peakRssKb = 0;
    bool falseNegativeSuspect = false;
    // Why a "blocked" run is blocked: targets inside search_radius but out of reach,
    // and how many of them have any solid block between them and the player.
    std::size_t blockedTargets = 0;
    std::size_t obscuredTargets = 0;
    // Harvest level check: the highest required tier among the targets, and the best
    // pickaxe the scenario hotbar can offer. When required > available the run cannot
    // produce drops, and it says so instead of calling the target "unreachable".
    int requiredToolTier = 0;
    int bestToolTier = 0;
    bool toolInsufficient = false;

    // Determinism control. The core plans paths on a background thread with a
    // wall-clock budget, so the exact tick a path arrives is not reproducible. The
    // runner therefore waits for planning to settle before each tick: with these
    // fields fixed between runs, ticks/breaks/positions are reproducible too.
    // deterministic=true means every planned path was awaited; false means at least
    // one wait exceeded its budget and the run's tick counts are timing-dependent.
    bool deterministic = true;
    int planWaits = 0;
    int planWaitTimeouts = 0;

    json::Value toJson() const;
};

// Throws std::exception on invalid scenario construction / core errors.
RunResult runScenario(const Scenario& scenario);

}  // namespace mwi::sim
