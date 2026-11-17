#include "sim/runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

#include <sys/resource.h>

#include "baritone/Baritone.h"
#include "baritone/behavior/InventoryBehavior.h"
#include "platform/BlockClassifier.h"
#include "baritone/Settings.h"
#include "baritone/cache/WorldScanner.h"
#include "baritone/process/MineProcess.h"
#include "baritone/process/TunnelProcess.h"
#include "sim/world.h"

namespace mwi::sim {
namespace {

double timevalMs(const timeval& value) {
    return static_cast<double>(value.tv_sec) * 1000.0 +
           static_cast<double>(value.tv_usec) / 1000.0;
}

// The scenario hotbar is the bot's whole hotbar; the core's autoTool only moves the
// selected slot. Without this, a pickaxe in slot 0 never gets used for a block that
// wants a shovel/axe, and harvest levels look wrong through no fault of the bot.
void selectBestTool(HeadlessPlayer& player, const baritone::BlockProps& block) {
    const bool needsTool = block.requiresTool || block.bestTool != baritone::Tool::None;
    if (!needsTool) return;
    int bestSlot = -1;
    double bestSpeed = -1.0;
    for (int slot = 0; slot < 9; ++slot) {
        const baritone::HotbarStack stack = player.hotbar(slot);
        if (stack.empty()) continue;
        if (block.bestTool != baritone::Tool::None && stack.tool != block.bestTool) continue;
        const double speed = baritone::InventoryBehavior::breakSpeed(stack, block);
        if (speed > bestSpeed) {
            bestSpeed = speed;
            bestSlot = slot;
        }
    }
    if (bestSlot < 0) bestSlot = 0;  // nothing suitable: keep what is held
    if (player.selectedSlot() != bestSlot) player.selectSlot(bestSlot);
}

void configureSettings(const Scenario& scenario) {
    baritone::Settings& s = baritone::Settings::get();
    // Headless runs must not write a chunk cache, wander into unloaded chunks, or
    // place blocks unless the scenario explicitly asks for it.
    s.chunkCaching = false;
    s.exploreForBlocks = scenario.explore;
    s.allowPlace = scenario.allowPlace;
    s.allowParkour = scenario.allowParkour;
    s.allowDownward = false;
    s.allowBreak = true;
    s.autoTool = true;
    s.mineScanDroppedItems = false;
    s.legitMine = false;
    s.maxScanChunkRadius = 4;
    s.blockReachDistance = scenario.reach;
    s.censorCoordinates = false;
    s.desktopNotifications = false;
    s.notificationOnMineFail = false;
    s.avoidance = false;
    // The core ships a default avoided list (fire, lava, magma, cactus...). A scenario
    // that explicitly places such a block is asking for it to be mined; leaving the
    // default in makes the bot log "No path" instead of doing what was asked.
    s.blocksToAvoidBreaking = {};
    s.blocksToDisallowBreaking = {};
    s.avoidUpdatingFallingBlocks = false;
    s.strictLiquidCheck = false;
    // Path calculation runs in a background thread. The budget is wall-clock, so a
    // 5 ms primary timeout made any slightly expensive path (hardness 50 obsidian,
    // a bigger search area) fail as "No path" instead of planning. Bound the run with
    // max_ticks and give planning a realistic budget.
    s.primaryTimeoutMS = 1500;
    s.failureTimeoutMS = 5000;
    s.planAheadPrimaryTimeoutMS = 500;
    s.planAheadFailureTimeoutMS = 2000;
}

baritone::Direction directionFromName(const std::string& name) {
    if (name == "north") return baritone::Direction::North;
    if (name == "south") return baritone::Direction::South;
    if (name == "west") return baritone::Direction::West;
    if (name == "east") return baritone::Direction::East;
    throw std::runtime_error("unsupported tunnel direction: " + name);
}

// True when the straight line from the player's eye to the target's centre runs
// through a solid block before the target itself. A small deterministic grid walk;
// this is a reachability hint, not the core's exact collision sweep.
bool lineObscured(const World& world, const baritone::Vec3d& eye, const baritone::BlockPos& target) {
    const baritone::Vec3d to{target.x + 0.5, target.y + 0.5, target.z + 0.5};
    const double dx = to.x - eye.x, dy = to.y - eye.y, dz = to.z - eye.z;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length < 1e-6) return false;
    const int steps = std::max(1, static_cast<int>(length * 8.0));
    baritone::BlockPos last{0, 0, 0};
    bool haveLast = false;
    for (int i = 1; i < steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const baritone::BlockPos cell{static_cast<int>(std::floor(eye.x + dx * t)),
                                      static_cast<int>(std::floor(eye.y + dy * t)),
                                      static_cast<int>(std::floor(eye.z + dz * t))};
        if (haveLast && cell == last) continue;
        last = cell;
        haveLast = true;
        if (cell == target) return false;
        if (world.isSolid(cell.x, cell.y, cell.z)) return true;
    }
    return false;
}

// Highest harvest level any target needs, and the best pickaxe the hotbar holds.
// Uses the same table as InventoryBehavior::breakSpeed so a "no drop" report and the
// bot's own decision can never disagree.
int requiredTierForTargets(const baritone::BlockFilter& filter) {
    int required = 0;
    for (const std::string& name : filter.names()) {
        baritone::BlockProps props = baritone::platform::classifyBlock(name, nullptr);
        if (!props.requiresTool) continue;
        required = std::max(required, baritone::requiredHarvestTier(props));
    }
    return required;
}

int bestPickaxeTier(const Scenario& scenario) {
    int best = 0;
    for (const HotbarItem& item : scenario.hotbar) {
        if (item.tool == "pickaxe") best = std::max(best, item.toolTier);
    }
    return best;
}

bool hasBlockedLog(const std::vector<LogLine>& logs) {
    return std::ranges::any_of(logs, [](const LogLine& line) {
        return line.error && (line.message.contains("blocked") ||
                              line.message.contains("No path") ||
                              line.message.contains("No known"));
    });
}

}  // namespace

RunResult runScenario(const Scenario& scenario) {
    const auto wallStart = std::chrono::steady_clock::now();
    rusage before{};
    if (getrusage(RUSAGE_SELF, &before) != 0) before = {};

    RunResult result;
    result.scenario = scenario;
    result.direction = directionName(scenario);
    result.databaseBytes = 0;

    {
        configureSettings(scenario);
        baritone::BlockFilter filter(scenario.targets);
        World world(scenario);
        world.cacheInitialTargets(filter);
        HeadlessPlayer player(scenario, world, filter);
        std::vector<LogLine> logs;

        baritone::Baritone bot(player, [&logs](const std::string& message, bool error) {
            logs.push_back(LogLine{message, error});
        });

        // The core expects a published snapshot before a process command starts.
        bot.tick(world.snapshot());
        // Baritone lifts the bot out of a solid block before the first tick; take that
        // resolved footing so gravity does not drag the player down through the floor.
        player.setGroundY(player.footingY(static_cast<int>(std::floor(player.position().x)),
                                          static_cast<int>(std::floor(player.position().z))));
        if (scenario.mode == Mode::Mine) {
            bot.mine().mine(scenario.quantity, filter);
        } else {
            bot.tunnel().start(directionFromName(result.direction), scenario.length);
        }

        bool active = true;
        int ticks = 0;
        // Let the very first plan land before the first simulated tick.
        const auto settle = [&](int budgetMs) {
            const auto start = std::chrono::steady_clock::now();
            while (bot.pathing().isCalculating()) {
                if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count() > budgetMs) {
                    ++result.planWaitTimeouts;
                    result.deterministic = false;
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ++result.planWaits;
        };
        settle(10000);
        while (ticks < scenario.maxTicks) {
            player.setTick(ticks);
            selectBestTool(player, world.props({static_cast<int>(std::floor(player.position().x)),
                                                static_cast<int>(std::floor(player.position().y)) - 1,
                                                static_cast<int>(std::floor(player.position().z))}));
            bot.tick(world.snapshot());
            player.applyControls(bot.controls());
            ++ticks;
            active = scenario.mode == Mode::Mine ? bot.mine().isActive() : bot.tunnel().isActive();
            const bool collected = scenario.mode == Mode::Mine && scenario.quantity > 0 &&
                                   player.collectedEnough(scenario.quantity);
            if (!active || collected) break;
            // Wait for the planner before the next tick, so the same scenario does the
            // same work every run. tick_delay_ms remains the per-tick pause.
            settle(scenario.planBudgetMs);
            if (scenario.tickDelayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(scenario.tickDelayMs));
            } else {
                std::this_thread::yield();
            }
        }

        const bool collected = scenario.mode == Mode::Mine && scenario.quantity > 0 &&
                               player.collectedEnough(scenario.quantity);
        const std::size_t initialTargets = world.initialTargetCount(filter);
        const bool blockedLog = hasBlockedLog(logs);
        const bool noTarget = scenario.mode == Mode::Mine && initialTargets == 0 && !scenario.explore;

        // Distinguish "the world has no such block" from "the block is there but cannot
        // be reached". The default chunk_radius is small, so searching the whole
        // snapshot here would call a buried deposit "absent".
        std::vector<baritone::BlockPos> seenTargets, seenObstacles;
        world.collectSearchResults(filter, scenario.searchRadius, seenTargets, seenObstacles);
        const double eyeY = player.position().y + 1.62;
        for (const baritone::BlockPos& pos : seenTargets) {
            const double distance = std::sqrt(
                std::pow(pos.x + 0.5 - player.position().x, 2) +
                std::pow(pos.y + 0.5 - player.position().y, 2) +
                std::pow(pos.z + 0.5 - player.position().z, 2));
            if (distance <= scenario.reach) continue;
            ++result.blockedTargets;
            if (lineObscured(world, {player.position().x, eyeY, player.position().z}, pos)) {
                ++result.obscuredTargets;
            }
        }
        // Only call it unreachable when a target really is out of reach. A target that
        // simply had no time inside max_ticks is a timeout, not a refusal.
        const bool unreachable = scenario.mode == Mode::Mine && result.blockedTargets > 0 &&
                                 !collected && player.metrics().targetBlocksBroken == 0;
        result.requiredToolTier = requiredTierForTargets(filter);
        result.bestToolTier = bestPickaxeTier(scenario);
        result.toolInsufficient = result.requiredToolTier > result.bestToolTier &&
                                  player.metrics().dropsCollected == 0;

        const bool budgetExhausted = ticks >= scenario.maxTicks && active;
        if (noTarget) result.status = "no_target";
        else if (result.toolInsufficient) result.status = "tool_insufficient";
        else if (!active || collected) result.status = "completed";
        // Exhausting max_ticks is a timeout even if shutting the bot down logged a
        // "no path" for a plan that never got its full budget.
        else if (budgetExhausted) result.status = "timeout";
        else if (blockedLog || unreachable) result.status = "blocked";
        else result.status = "timeout";

        result.finished = result.status == "completed" || result.status == "no_target";
        result.botActive = active;
        result.ticks = ticks;
        result.finalPosition = player.position();
        result.finalYaw = player.rotation().yaw;
        result.finalPitch = player.rotation().pitch;
        result.selectedSlot = player.selectedSlot();
        result.breaks = player.breakEvents();
        result.inventory = player.inventorySnapshot();
        result.logs = std::move(logs);
        result.playerMetrics = player.metrics();
        result.initialTargets = initialTargets;
        result.remainingTargets = world.remainingTargetCount(filter);
        result.worldSnapshotBytes = world.estimateSnapshotBytes();
        result.loadedChunks = world.loadedChunkCount();
        result.falseNegativeSuspect = !seenTargets.empty() &&
                                      result.playerMetrics.targetBlocksBroken == 0 &&
                                      (result.status == "timeout" || result.status == "blocked");
        // bot, player, and world destruct here: Baritone::shutdown() joins its
        // pathfinding thread before wall/CPU measurement is finalized.
    }

    rusage after{};
    if (getrusage(RUSAGE_SELF, &after) != 0) after = {};
    const auto wallEnd = std::chrono::steady_clock::now();
    result.wallMs = std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();
    result.cpuMs = (timevalMs(after.ru_utime) + timevalMs(after.ru_stime)) -
                   (timevalMs(before.ru_utime) + timevalMs(before.ru_stime));
    result.peakRssKb = static_cast<std::uint64_t>(after.ru_maxrss);
    return result;
}

json::Value RunResult::toJson() const {
    json::Array breaks;
    for (const BreakEvent& event : this->breaks) {
        breaks.push_back(json::object({
            {"tick", json::Value::makeNumber(event.tick)},
            {"x", json::Value::makeNumber(event.x)},
            {"y", json::Value::makeNumber(event.y)},
            {"z", json::Value::makeNumber(event.z)},
            {"block", json::Value::makeString(event.block)},
            {"face", json::Value::makeString(event.face)},
            {"drop", json::Value::makeString(event.drop)},
            {"target_hit", json::Value::makeBool(event.targetHit)},
            {"tool_satisfied", json::Value::makeBool(event.toolSatisfied)},
            {"clicks", json::Value::makeNumber(event.clicks)},
            {"note", json::Value::makeString(event.note)},
        }));
    }

    json::Array logs;
    for (const LogLine& line : this->logs) {
        logs.push_back(json::object({
            {"message", json::Value::makeString(line.message)},
            {"error", json::Value::makeBool(line.error)},
        }));
    }

    json::Object inventory;
    for (const auto& [name, count] : this->inventory) {
        inventory[name] = std::make_shared<json::Value>(json::Value::makeNumber(count));
    }

    json::Array limitations;
    if (toolInsufficient) {
        limitations.push_back(json::Value::makeString(
            "The hotbar's best pickaxe tier (" + std::to_string(bestToolTier) +
            ") is below the target's harvest level (" + std::to_string(requiredToolTier) +
            "); no drops can be collected in this scenario."));
    }
    for (const char* text : {
             "Headless in-memory simulation; no Minecraft Bedrock process is attached.",
             "Drop and tool rules are deterministic simplifications; no Fortune, Silk Touch, durability loss, or random drops.",
             "Drops are collected immediately; entities(), container inventory, and multiplayer are not simulated.",
             "Background path calculation uses real time; tick_delay_ms bounds execution but does not make thread scheduling deterministic.",
             "No persistent database is used; world_snapshot_estimate_bytes is an in-memory estimate.",
         }) {
        limitations.push_back(json::Value::makeString(text));
    }

    return json::object({
        {"schema_version", json::Value::makeNumber(kResultSchemaVersion)},
        {"engine_version", json::Value::makeString(kEngineVersion)},
        {"status", json::Value::makeString(status)},
        {"finished", json::Value::makeBool(finished)},
        {"bot_active", json::Value::makeBool(botActive)},
        {"mode", json::Value::makeString(scenario.modeName())},
        {"direction", json::Value::makeString(direction)},
        {"scenario", scenario.summary()},
        {"ticks", json::Value::makeNumber(ticks)},
        {"player", json::object({
            {"x", json::Value::makeNumber(finalPosition.x)},
            {"y", json::Value::makeNumber(finalPosition.y)},
            {"z", json::Value::makeNumber(finalPosition.z)},
            {"yaw", json::Value::makeNumber(finalYaw)},
            {"pitch", json::Value::makeNumber(finalPitch)},
            {"selected_slot", json::Value::makeNumber(selectedSlot)},
        })},
        {"targets", json::object({
            {"initial", json::Value::makeNumber(static_cast<double>(initialTargets))},
            {"remaining", json::Value::makeNumber(static_cast<double>(remainingTargets))},
            {"blocked", json::Value::makeNumber(static_cast<double>(blockedTargets))},
            {"obscured", json::Value::makeNumber(static_cast<double>(obscuredTargets))},
            {"search_radius", json::Value::makeNumber(scenario.searchRadius)},
        })},
        {"tools", json::object({
            {"required_tier", json::Value::makeNumber(requiredToolTier)},
            {"best_tier", json::Value::makeNumber(bestToolTier)},
            {"insufficient", json::Value::makeBool(toolInsufficient)},
        })},
        {"breaks", json::Value::makeArray(std::move(breaks))},
        {"inventory", json::Value::makeObject(std::move(inventory))},
        {"logs", json::Value::makeArray(std::move(logs))},
        {"counts", json::object({
            {"clicks", json::Value::makeNumber(static_cast<double>(playerMetrics.clicks))},
            {"unresolved_clicks", json::Value::makeNumber(static_cast<double>(playerMetrics.unresolvedClicks))},
            {"refused_breaks", json::Value::makeNumber(static_cast<double>(playerMetrics.refusedBreaks))},
            {"context_break_calls", json::Value::makeNumber(static_cast<double>(playerMetrics.contextBreakCalls))},
            {"target_blocks_broken", json::Value::makeNumber(static_cast<double>(playerMetrics.targetBlocksBroken))},
            {"obstacle_blocks_broken", json::Value::makeNumber(static_cast<double>(playerMetrics.obstacleBlocksBroken))},
            {"drops_collected", json::Value::makeNumber(static_cast<double>(playerMetrics.dropsCollected))},
            {"false_positive_suspects", json::Value::makeNumber(static_cast<double>(playerMetrics.falsePositiveSuspects))},
        })},
        {"quality", json::object({
            {"false_negative_suspect", json::Value::makeBool(falseNegativeSuspect)},
            {"false_positive_suspects", json::Value::makeNumber(static_cast<double>(playerMetrics.falsePositiveSuspects))},
            {"status_is_auditable", json::Value::makeBool(true)},
        })},
        {"resources", json::object({
            {"wall_ms", json::Value::makeNumber(wallMs)},
            {"cpu_ms", json::Value::makeNumber(cpuMs)},
            {"peak_rss_kb", json::Value::makeNumber(static_cast<double>(peakRssKb))},
            {"world_snapshot_estimate_bytes", json::Value::makeNumber(static_cast<double>(worldSnapshotBytes))},
            {"loaded_chunks", json::Value::makeNumber(static_cast<double>(loadedChunks))},
            {"database_bytes", json::Value::makeNumber(static_cast<double>(databaseBytes))},
        })},
        {"determinism", json::object({
            {"deterministic", json::Value::makeBool(deterministic)},
            {"plan_waits", json::Value::makeNumber(planWaits)},
            {"plan_wait_timeouts", json::Value::makeNumber(planWaitTimeouts)},
            {"plan_budget_ms", json::Value::makeNumber(scenario.planBudgetMs)},
        })},
        {"limitations", json::Value::makeArray(std::move(limitations))},
    });
}

}  // namespace mwi::sim
