#include "platform/BaritoneRuntime.h"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/pathing/goals/Goal.h"
#include "platform/GamePlayerContext.h"
#include "platform/InputOverride.h"
#include "platform/PathVisualizer.h"
#include "platform/Log.h"
#include "platform/Paths.h"
#include "platform/WorldTracker.h"
#include "sdk/Components.h"
#include "sdk/Game.h"
#include "sdk/Memory.h"
#include "sdk/Offsets.h"

namespace baritone::platform {
namespace {

using InputBits = std::bitset<static_cast<std::size_t>(baritone::Input::Count)>;

std::string inputNames(const InputBits& inputs) {
    static constexpr const char* names[] = {"forward", "back", "left", "right", "clickL",
                                            "clickR",  "jump", "sneak", "sprint"};
    std::string out;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (!inputs.test(i)) continue;
        if (!out.empty()) out += ',';
        out += names[i];
    }
    return out.empty() ? "none" : out;
}

const char* statusName(baritone::MovementStatus status) {
    switch (status) {
        case baritone::MovementStatus::Prepping: return "Prepping";
        case baritone::MovementStatus::Waiting: return "Waiting";
        case baritone::MovementStatus::Running: return "Running";
        case baritone::MovementStatus::Success: return "Success";
        case baritone::MovementStatus::Unreachable: return "Unreachable";
        case baritone::MovementStatus::Failed: return "Failed";
        case baritone::MovementStatus::Canceled: return "Canceled";
    }
    return "?";
}

std::string rotationText(const baritone::MovementTarget& target) {
    if (!target.rotation) return "none";
    return std::format("yaw={:.2f},pitch={:.2f},force={}", target.rotation->yaw,
                       target.rotation->pitch, target.forceRotations);
}

std::string moveStateText(const sdk::MoveInputState& s) {
    return std::format(
        "up={} down={} left={} right={} jmp={} snk={} spr={} analog=({:.2f},{:.2f})",
        static_cast<bool>(s.up), static_cast<bool>(s.down), static_cast<bool>(s.left),
        static_cast<bool>(s.right), static_cast<bool>(s.jumpDown),
        static_cast<bool>(s.sneakDown), static_cast<bool>(s.sprintDown), s.analogMoveVector.x,
        s.analogMoveVector.y);
}

std::string blockText(sdk::BlockSource& region, const sdk::BlockPos& pos) {
    const sdk::Block block(region.getBlock(pos));
    std::string name = block.name();
    const std::string legacy = block.legacyName();
    if (name.empty()) name = legacy.empty() ? "<unreadable>" : legacy;
    return std::format("ptr={} name='{}' legacy='{}'", block.raw(), name, legacy);
}

void logRegistryLayout(const char* tag, void* registry) {
    if (!registry) {
        logger::info("{} registry=null", tag);
        return;
    }

    const bool readable = mem::isReadable(registry, 0x90);
    logger::info("{} registry={} raw=0x{:X} readable90={}", tag, registry,
                 mem::addressOf(registry), readable);
    if (!readable) return;

    const auto* base = static_cast<const std::uint8_t*>(registry);
    const auto sparseBegin = mem::read<const std::uint64_t*>(base, 0x60);
    const auto sparseEnd = mem::read<const std::uint64_t*>(base, 0x68);
    const auto packedBegin = mem::read<const std::uint64_t*>(base, 0x78);
    const auto packedEnd = mem::read<const std::uint64_t*>(base, 0x80);
    const auto buckets = (sparseBegin && sparseEnd >= sparseBegin)
                             ? static_cast<std::uint64_t>(sparseEnd - sparseBegin)
                             : 0;
    const auto nodes = (packedBegin && packedEnd >= packedBegin)
                           ? static_cast<std::uint64_t>(packedEnd - packedBegin)
                           : 0;
    logger::info("{} map sb={} se={} pb={} pe={} buckets={} nodes={}", tag,
                 static_cast<const void*>(sparseBegin), static_cast<const void*>(sparseEnd),
                 static_cast<const void*>(packedBegin), static_cast<const void*>(packedEnd),
                 buckets, nodes);

    const sdk::GameRegistry reg(registry);
    const auto show = [&](const char* name, std::uint32_t hash) {
        logger::info("{} storage {:<18} 0x{:08X} = {}", tag, name, hash,
                     static_cast<void*>(reg.storage(hash)));
    };
    show("StateVector", sdk::StateVectorComponent::kTypeHash);
    show("AABBShape", sdk::AABBShapeComponent::kTypeHash);
    show("ActorRotation", sdk::ActorRotationComponent::kTypeHash);
    show("MoveInput", sdk::MoveInputComponent::kTypeHash);
    show("MovementAbilities", sdk::MovementAbilitiesComponent::kTypeHash);
    show("ActorOwner", sdk::ActorOwnerComponent::kTypeHash);
    show("LocalPlayer", sdk::LocalPlayerComponent::kTypeHash);
    show("Player", sdk::PlayerComponent::kTypeHash);
    show("ActorGameType", sdk::ActorGameTypeComponent::kTypeHash);
}

void logFirstTickDetails(const sdk::GameRegistry& reg, EntityId entity, void* actorPtr,
                         void* regionPtr, GamePlayerContext& context) {
    logger::info("FIRST entity=0x{:08X} actor={} actorRaw=0x{:X} blockSource={} bsRaw=0x{:X}",
                 entity.raw, actorPtr, mem::addressOf(actorPtr), regionPtr,
                 mem::addressOf(regionPtr));

    sdk::Actor actor(actorPtr);
    if (const auto* gameContext = actor.entityContext();
        gameContext && mem::isReadable(gameContext, sizeof(*gameContext))) {
        logger::info(
            "FIRST entityContext owner={} registry={} entity=0x{:08X} matchesTick={}",
            gameContext->owner, gameContext->registry, gameContext->entity.raw,
            gameContext->registry == reg.raw());
    } else {
        logger::warn("FIRST entityContext unreadable actor={}", actorPtr);
    }

    auto* dimension = mem::read<void*>(actorPtr, offsets::Actor_dimension);
    const bool dimensionReadable =
        dimension && mem::isReadable(dimension, offsets::Dimension_blockSource + sizeof(void*));
    auto* dimensionBlocks =
        dimensionReadable ? mem::read<void*>(dimension, offsets::Dimension_blockSource) : nullptr;
    logger::info(
        "FIRST dimension +0x{:X}={} dimReadable={} blockSource={} bsReadable={}",
        offsets::Actor_dimension, dimension, dimensionReadable, dimensionBlocks,
        mem::isReadable(dimensionBlocks, 0x100));

    const auto pos = context.position();
    const auto eye = context.eyePosition();
    const auto velocity = context.velocity();
    const auto rotation = context.rotation();
    logger::info(
        "FIRST context valid={} pos=({:.3f},{:.3f},{:.3f}) eye=({:.3f},{:.3f},{:.3f}) "
        "vel=({:.3f},{:.3f},{:.3f}) rot=(yaw {:.2f}, pitch {:.2f}) ground={} water={} sneak={} "
        "fly={} creative={} food={} hp={:.1f} reach={:.2f}",
        context.valid(), pos.x, pos.y, pos.z, eye.x, eye.y, eye.z, velocity.x, velocity.y,
        velocity.z, rotation.yaw, rotation.pitch, context.onGround(), context.isInWater(),
        context.isSneaking(), context.isFlying(), context.isCreative(), context.foodLevel(),
        context.health(), context.blockReachDistance());

    if (const auto* state = reg.tryGet<sdk::StateVectorComponent>(entity)) {
        logger::info(
            "FIRST StateVector ptr={} pos=({:.3f},{:.3f},{:.3f}) old=({:.3f},{:.3f},{:.3f}) "
            "vel=({:.3f},{:.3f},{:.3f})",
            static_cast<const void*>(state), state->pos.x, state->pos.y, state->pos.z,
            state->posOld.x, state->posOld.y, state->posOld.z, state->velocity.x,
            state->velocity.y, state->velocity.z);
    } else {
        logger::warn("FIRST StateVectorComponent missing");
    }

    if (const auto* box = reg.tryGet<sdk::AABBShapeComponent>(entity)) {
        logger::info(
            "FIRST AABB ptr={} min=({:.3f},{:.3f},{:.3f}) max=({:.3f},{:.3f},{:.3f}) "
            "size=({:.2f},{:.2f})",
            static_cast<const void*>(box), box->box.min.x, box->box.min.y, box->box.min.z,
            box->box.max.x, box->box.max.y, box->box.max.z, box->size.x, box->size.y);
    } else {
        logger::warn("FIRST AABBShapeComponent missing");
    }

    if (const auto* rot = reg.tryGet<sdk::ActorRotationComponent>(entity)) {
        logger::info("FIRST ActorRotation ptr={} rot=({:.3f},{:.3f}) old=({:.3f},{:.3f})",
                     static_cast<const void*>(rot), rot->rotation.x, rot->rotation.y,
                     rot->rotationOld.x, rot->rotationOld.y);
    } else {
        logger::warn("FIRST ActorRotationComponent missing");
    }

    if (const auto* input = reg.tryGet<sdk::MoveInputComponent>(entity)) {
        logger::info("FIRST MoveInput ptr={} move=({:.3f},{:.3f}) lookDelta=({:.3f},{:.3f}) "
                     "camera=({:.3f},{:.3f},{:.3f}) input[{}] raw[{}] jumping={} sneaking={} "
                     "sprinting={} cameraRelative={} autoJump={}",
                     static_cast<const void*>(input), input->move.x, input->move.y,
                     input->lookDelta.x, input->lookDelta.y, input->cameraOrientation.x,
                     input->cameraOrientation.y, input->cameraOrientation.z,
                     moveStateText(input->inputState), moveStateText(input->rawInputState),
                     static_cast<bool>(input->jumping), static_cast<bool>(input->sneaking),
                     static_cast<bool>(input->sprinting),
                     static_cast<bool>(input->isCameraRelativeMovementEnabled),
                     static_cast<bool>(input->autoJumpEnabled));
    } else {
        logger::warn("FIRST MoveInputComponent missing");
    }

    if (const auto* abilities = reg.tryGet<sdk::MovementAbilitiesComponent>(entity)) {
        logger::info("FIRST MovementAbilities ptr={} flags=0x{:02X} flying={}",
                     static_cast<const void*>(abilities),
                     abilities->data[offsets::MovementAbilities_flags],
                     (abilities->data[offsets::MovementAbilities_flags] & 1u) != 0);
    } else {
        logger::warn("FIRST MovementAbilitiesComponent missing");
    }

    if (const auto* gameType = reg.tryGet<sdk::ActorGameTypeComponent>(entity)) {
        logger::info("FIRST ActorGameType ptr={} gameType={}", static_cast<const void*>(gameType),
                     gameType->gameType);
    } else {
        logger::warn("FIRST ActorGameTypeComponent missing");
    }

    sdk::BlockSource region(regionPtr);
    const sdk::BlockPos feet = sdk::BlockPos::floor(
        {static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)});
    for (int dy = -2; dy <= 2; ++dy) {
        const sdk::BlockPos p{feet.x, feet.y + dy, feet.z};
        logger::info("FIRST block feet{:+d} ({},{},{}) {}", dy, p.x, p.y, p.z,
                     blockText(region, p));
    }
    const sdk::BlockPos head{feet.x, feet.y + 2, feet.z};
    logger::info("FIRST head ({},{},{}) {}", head.x, head.y, head.z, blockText(region, head));
}

std::string pathInfoText(
    const std::optional<std::pair<baritone::BlockPos, baritone::GoalPtr>>& info) {
    if (!info) return "none";
    return std::format("dest=({},{},{}) goal='{}'", info->first.x, info->first.y, info->first.z,
                       info->second ? info->second->toString() : "?");
}

std::string goalText(const baritone::GoalPtr& goal) {
    return goal ? goal->toString() : "<none>";
}

}  // namespace

BaritoneRuntime::BaritoneRuntime() = default;
BaritoneRuntime::~BaritoneRuntime() = default;

BaritoneRuntime& BaritoneRuntime::get() {
    static BaritoneRuntime instance;
    return instance;
}

void BaritoneRuntime::initialize(const std::filesystem::path& dataDir) {
    if (initialized_) return;

    logger::setLogFile(dataDir / "baritone.log");
    logger::info("Baritone runtime initializing (build {} {})...", __DATE__, __TIME__);
    logger::info("Runtime data dir: {}", dataDir.string());
    paths::setDataDir(dataDir);
    context_ = std::make_unique<GamePlayerContext>();
    baritone_ = std::make_unique<baritone::Baritone>(
        *context_, [](const std::string& message, bool error) {
            if (error) logger::error("{}", message);
            else logger::info("{}", message);
        });
    input_ = std::make_unique<InputOverride>();
    visualizer_ = std::make_unique<PathVisualizer>();

    auto& tracker = WorldTracker::get();
    tracker.loadRadius = 2;
    tracker.refreshRadius = 0;
    tracker.refreshTicks = 40;
    tracker.budgetMs = 2.0;

    const auto& settings = baritone::Settings::get();
    logger::info(
        "Settings allowBreak={} allowPlace={} allowSprint={} allowParkour={} "
        "assumeWalkOnWater={} freeLook={} antiCheatCompatibility={} chunkCaching={} "
        "reach={:.2f} maxFall={}",
        settings.allowBreak, settings.allowPlace, settings.allowSprint, settings.allowParkour,
        settings.assumeWalkOnWater, settings.freeLook, settings.antiCheatCompatibility,
        settings.chunkCaching, settings.blockReachDistance, settings.maxFallHeightNoWater);
    logger::info("WorldTracker loadRadius={} refreshRadius={} refreshTicks={} budgetMs={:.1f}",
                 tracker.loadRadius, tracker.refreshRadius, tracker.refreshTicks,
                 tracker.budgetMs);

    initialized_ = true;
    logger::info("Baritone runtime initialized");
}

void BaritoneRuntime::shutdown() {
    if (!initialized_) return;

    logger::info("Shutting down runtime: ticks={} skipped={} firstTickLogged={}", ticks_,
                 skippedTicks_, firstTickLogged_);
    if (baritone_) baritone_->shutdown();
    if (visualizer_) visualizer_->shutdown();
    visualizer_.reset();
    input_.reset();
    if (context_) context_->unbind();
    baritone_.reset();
    context_.reset();
    WorldTracker::get().reset();
    {
        std::lock_guard lock(goalMutex_);
        pendingGoal_.reset();
    }
    initialized_ = false;
    logger::info("Baritone runtime shut down");
    logger::flush();
}

void BaritoneRuntime::startTestGoal() {
    pendingTestGoal_.store(true, std::memory_order_relaxed);
    logger::info("Test goal queued");
}

void BaritoneRuntime::startGoalFromText(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (char c : text) {
        if (c == ',' || c == '(' || c == ')' || c == '[' || c == ']') {
            normalized.push_back(' ');
        } else {
            normalized.push_back(c);
        }
    }

    std::istringstream stream(normalized);
    std::vector<double> values;
    double value = 0.0;
    while (stream >> value) values.push_back(value);
    if (!stream.eof() || (values.size() != 2 && values.size() != 3)) {
        logger::warn("GOAL invalid coordinates '{}'", text);
        return;
    }
    constexpr double kMinCoordinate = static_cast<double>(std::numeric_limits<int>::min());
    constexpr double kMaxCoordinate = static_cast<double>(std::numeric_limits<int>::max());
    for (double v : values) {
        if (!std::isfinite(v) || v < kMinCoordinate || v > kMaxCoordinate) {
            logger::warn("GOAL invalid coordinate range '{}'", text);
            return;
        }
    }

    GoalRequest request;
    request.x = static_cast<int>(std::floor(values[0]));
    if (values.size() == 3) {
        request.y = static_cast<int>(std::floor(values[1]));
        request.z = static_cast<int>(std::floor(values[2]));
        request.hasY = true;
    } else {
        request.z = static_cast<int>(std::floor(values[1]));
    }

    {
        std::lock_guard lock(goalMutex_);
        pendingGoal_ = request;
    }
    logger::info("GOAL queued text='{}' type={} x={} y={} z={}", text,
                 request.hasY ? "GoalBlock" : "GoalXZ", request.x, request.y, request.z);
}

void BaritoneRuntime::startMineFromText(std::string_view filterText, int quantity) {
    // "#mine iron_ore" / "#mine 64 iron_ore,diamond_ore" - same shape as Java Baritone:
    // an optional leading quantity, then one or more block names.
    std::string text;
    text.reserve(filterText.size());
    for (char c : filterText) {
        const bool sep = c == ',' || c == ';' || c == '(' || c == ')' || c == '[' || c == ']';
        text.push_back(sep ? ' ' : c);
    }

    std::istringstream stream(text);
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;) tokens.push_back(std::move(token));
    if (tokens.empty()) {
        logger::warn("MINE no block name in '{}'", filterText);
        return;
    }

    int parsedQuantity = quantity;
    std::size_t first = 0;
    {
        const std::string& head = tokens.front();
        bool allDigits = !head.empty() && head.size() <= 9 &&
                         std::ranges::all_of(head, [](char c) { return c >= '0' && c <= '9'; });
        if (allDigits) {
            parsedQuantity = std::stoi(head);
            first = 1;
        }
    }
    if (first >= tokens.size()) {
        logger::warn("MINE '{}' has a quantity but no block name", filterText);
        return;
    }

    std::string names;
    for (std::size_t i = first; i < tokens.size(); ++i) {
        if (!names.empty()) names += ',';
        names += tokens[i];
    }

    {
        std::lock_guard lock(goalMutex_);
        pendingMine_ = MineRequest{std::move(names), std::max(parsedQuantity, 0)};
        pendingGoal_.reset();  // one command at a time
    }
    logger::info("MINE queued blocks='{}' quantity={}", tokens[first],
                 std::max(parsedQuantity, 0));
}

void BaritoneRuntime::startTunnelFromText(std::string_view text) {
    // "#tunnel 64" / "#tunnel 64 north" / "#tunnel" (as far as it goes, this way)
    std::istringstream stream{std::string(text)};
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;) {
        for (char& c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        tokens.push_back(std::move(token));
    }

    TunnelRequest request;
    for (const std::string& token : tokens) {
        if (token == "north" || token == "south" || token == "east" || token == "west") {
            request.direction = token;
            continue;
        }
        bool digits = !token.empty() &&
                      std::ranges::all_of(token, [](char c) { return c >= '0' && c <= '9'; });
        if (digits && token.size() <= 6) request.length = std::stoi(token);
    }

    {
        std::lock_guard lock(goalMutex_);
        pendingTunnel_ = std::move(request);
        pendingGoal_.reset();
        pendingMine_.reset();
    }
    logger::info("TUNNEL queued direction='{}' length={}",
                 tokens.empty() ? "" : (request.direction.empty() ? "facing" : request.direction),
                 request.length);
}

void BaritoneRuntime::setFovCompensation(float degrees) {
    if (!std::isfinite(degrees)) return;
    const float clamped = std::clamp(degrees, -60.0f, 60.0f);
    fovCompensation_.store(clamped, std::memory_order_relaxed);
    logger::info("FOV compensation set to {:+.1f}", clamped);
}

void BaritoneRuntime::setTurnSensitivity(float sensitivity) {
    if (!std::isfinite(sensitivity)) return;
    const float clamped = std::clamp(sensitivity, 0.1f, 10.0f);
    turnSensitivity_.store(clamped, std::memory_order_relaxed);
    logger::info("Turn sensitivity set to {:.3f}", clamped);
}

void BaritoneRuntime::setManipulateCamera(bool enabled) {
    manipulateCamera_.store(enabled, std::memory_order_relaxed);
    logger::info("Manipulate camera {}", enabled ? "enabled" : "disabled");
}

void BaritoneRuntime::cancel() {
    pendingCancel_.store(true, std::memory_order_relaxed);
    logger::info("Cancel queued");
}

void BaritoneRuntime::beforeGameTick(void* registry) {
    if (!initialized_ || !input_) return;
    // Mining drives the same controls as pathing but needs no goal of its own, so it
    // has to be part of "the bot is doing something" or the break clicks are dropped.
    const bool active = baritone_ && (baritone_->pathing().isPathing() ||
                                      baritone_->pathing().isCalculating() ||
                                      baritone_->customGoal().goal() != nullptr ||
                                      baritone_->mine().isActive() ||
                                      baritone_->tunnel().isActive());
    input_->before(registry, lastControls_, active,
                   manipulateCamera_.load(std::memory_order_relaxed),
                   turnSensitivity_.load(std::memory_order_relaxed));
}

void BaritoneRuntime::afterGameTick(void* registry) {
    if (!initialized_ || !baritone_ || !context_) return;
    if (input_) input_->after();

    const auto skip = [&](std::string_view reason) {
        context_->unbind();
        ++skippedTicks_;
        const bool verbose = skippedTicks_ <= 3 || skippedTicks_ % 20 == 0;
        if (verbose) logger::warn("SKIP #{} tick={} reason='{}'", skippedTicks_, ticks_ + 1, reason);
        return verbose;
    };

    if (!registry) {
        skip("no registry");
        return;
    }

    sdk::GameRegistry reg(registry);
    if (!reg.valid()) {
        if (skip("invalid registry")) logRegistryLayout("  BADREG", registry);
        return;
    }

    auto entity = reg.playerEntity();
    if (!entity) {
        if (skip("no player entity")) {
            const auto local = reg.localPlayerEntity();
            const auto localStorage = reg.storage(sdk::LocalPlayerComponent::kTypeHash);
            const auto playerStorage = reg.storage(sdk::PlayerComponent::kTypeHash);
            logger::warn(
                "  LocalPlayer storage={} first=0x{:08X} count={} Player storage={} count={}",
                static_cast<void*>(localStorage), local ? local->raw : 0,
                reg.entities(sdk::LocalPlayerComponent::kTypeHash).size(),
                static_cast<void*>(playerStorage),
                reg.entities(sdk::PlayerComponent::kTypeHash).size());
        }
        return;
    }

    void* actorPtr = reg.actorFor(*entity);
    if (!actorPtr) {
        if (skip("no actor pointer")) {
            const auto* owner = reg.rawComponent(*entity, sdk::ActorOwnerComponent::kTypeHash);
            logger::warn(
                "  entity=0x{:08X} ActorOwner storage={} raw={} has={} actor={}",
                entity->raw, static_cast<void*>(reg.storage(sdk::ActorOwnerComponent::kTypeHash)),
                static_cast<const void*>(owner),
                reg.has(*entity, sdk::ActorOwnerComponent::kTypeHash),
                static_cast<const void*>(owner) ? *static_cast<void* const*>(owner) : nullptr);
        }
        return;
    }

    sdk::Actor actor(actorPtr);
    void* regionPtr = actor.blockSource(false);
    if (!regionPtr) {
        if (skip("no block source")) {
            logger::warn("  actor={} raw=0x{:X}", actorPtr, mem::addressOf(actorPtr));
            actor.blockSource(true);
        }
        return;
    }

    if (skippedTicks_ > 0) {
        logger::info("RECOVER tick={} after {} skipped ticks", ticks_ + 1, skippedTicks_);
        skippedTicks_ = 0;
    }

    context_->bind(registry, actorPtr, regionPtr, *entity);
    sdk::BlockSource region(regionPtr);

    const bool registryChanged = registry != lastRegistry_;
    if (registryChanged) lastRegistry_ = registry;
    if (!firstTickLogged_) {
        firstTickLogged_ = true;
        logFirstTickDetails(reg, *entity, actorPtr, regionPtr, *context_);
    } else if (registryChanged) {
        logRegistryLayout("REGCHANGE", registry);
    }
    if (actorPtr != lastActor_) {
        if (lastActor_) logger::info("ACTOR changed {} -> {} (raw=0x{:X})", lastActor_, actorPtr,
                                     mem::addressOf(actorPtr));
        lastActor_ = actorPtr;
    }
    if (regionPtr != lastBlockSource_) {
        if (lastBlockSource_)
            logger::info("BLOCKSOURCE changed {} -> {} (raw=0x{:X})", lastBlockSource_,
                         regionPtr, mem::addressOf(regionPtr));
        lastBlockSource_ = regionPtr;
    }

    if (pendingCancel_.exchange(false, std::memory_order_relaxed)) {
        baritone_->cancelEverything();
        logger::info("Cancelled");
    }
    if (pendingTestGoal_.exchange(false, std::memory_order_relaxed)) {
        const auto pos = context_->position();
        const sdk::BlockPos feet = sdk::BlockPos::floor(
            {static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)});
        const int targetX = feet.x + 16;
        const int targetZ = feet.z;
        const baritone::BlockPos targetLog{targetX, feet.y, targetZ};

        logger::info(
            "TESTGOAL from=({:.2f},{:.2f},{:.2f}) feet=({},{},{}) targetXZ=({},{}) dist={:.2f}",
            pos.x, pos.y, pos.z, feet.x, feet.y, feet.z, targetX, targetZ,
            std::hypot(static_cast<double>(targetX) - pos.x, static_cast<double>(targetZ) - pos.z));
        for (int dy = -3; dy <= 3; ++dy) {
            const sdk::BlockPos p{targetLog.x, targetLog.y + dy, targetLog.z};
            logger::info("TESTGOAL column y{:+d} ({},{},{}) {}", dy, p.x, p.y, p.z,
                         blockText(region, p));
        }

        baritone_->customGoal().setGoalAndPath(
            std::make_shared<baritone::GoalXZ>(targetX, targetZ));
        logger::info("TESTGOAL queued goal='{}' pathing={} calc={}",
                     goalText(baritone_->pathing().goal()),
                     baritone_->pathing().isPathing(),
                     baritone_->pathing().isCalculating());
    }

    std::optional<GoalRequest> goalRequest;
    {
        std::lock_guard lock(goalMutex_);
        goalRequest = std::move(pendingGoal_);
        pendingGoal_.reset();
    }
    if (goalRequest) {
        if (goalRequest->hasY) {
            baritone_->customGoal().setGoalAndPath(std::make_shared<baritone::GoalBlock>(
                goalRequest->x, goalRequest->y, goalRequest->z));
        } else {
            baritone_->customGoal().setGoalAndPath(
                std::make_shared<baritone::GoalXZ>(goalRequest->x, goalRequest->z));
        }
        logger::info("GOAL applied type={} x={} y={} z={} pathing={} calc={}",
                     goalRequest->hasY ? "GoalBlock" : "GoalXZ", goalRequest->x, goalRequest->y,
                     goalRequest->z, baritone_->pathing().isPathing(),
                     baritone_->pathing().isCalculating());
    }

    std::optional<MineRequest> mineRequest;
    {
        std::lock_guard lock(goalMutex_);
        mineRequest = std::move(pendingMine_);
        pendingMine_.reset();
    }
    if (mineRequest) {
        // MineProcess scans the world snapshot in mine(), so the snapshot for this tick
        // must be published first. It is built below, so queue for the next tick instead
        // of passing a stale one.
        pendingMineNextTick_ = std::move(mineRequest);
    }

    std::optional<TunnelRequest> tunnelRequest;
    {
        std::lock_guard lock(goalMutex_);
        tunnelRequest = std::move(pendingTunnel_);
        pendingTunnel_.reset();
    }
    if (tunnelRequest) pendingTunnelNextTick_ = std::move(tunnelRequest);

    auto& tracker = WorldTracker::get();
    const int dimension = 0;
    const baritone::Vec3d pos = context_->position();
    tracker.tick(region.raw(), pos, dimension, -64, 320);
    auto snapshot = tracker.snapshot();

    baritone_->tick(snapshot);
    lastControls_ = baritone_->controls();

    if (pendingTunnelNextTick_) {
        TunnelRequest request = std::move(*pendingTunnelNextTick_);
        pendingTunnelNextTick_.reset();
        baritone::Direction dir = baritone::Direction::North;
        if (request.direction == "south") {
            dir = baritone::Direction::South;
        } else if (request.direction == "east") {
            dir = baritone::Direction::East;
        } else if (request.direction == "west") {
            dir = baritone::Direction::West;
        } else if (request.direction.empty()) {
            // No direction given: take the way the player is already looking.
            const float yaw = context_->rotation().yaw;
            dir = baritone::nearestHorizontal(yaw);
        }
        baritone_->tunnel().start(dir, request.length);
        logger::info("TUNNEL started length={}", request.length);
    }

    if (pendingMineNextTick_ && baritone_->world()) {
        MineRequest request = std::move(*pendingMineNextTick_);
        pendingMineNextTick_.reset();
        std::string names = request.names;
        std::vector<std::string> list;
        std::size_t start = 0;
        while (start <= names.size()) {
            const std::size_t comma = names.find(',', start);
            const std::size_t end = comma == std::string::npos ? names.size() : comma;
            if (end > start) list.push_back(names.substr(start, end - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (list.empty()) {
            logger::warn("MINE dropped: no block names left after parsing '{}'", request.names);
        } else {
            baritone::BlockFilter filter(list);
            baritone_->mine().mine(request.quantity, filter);
            logger::info("MINE started filter='{}' quantity={} active={}", filter.toString(),
                         request.quantity, baritone_->mine().isActive());
        }
    }
    ++ticks_;

    {
        auto& pathing = baritone_->pathing();
        const bool renderActive = pathing.isPathing() || pathing.isCalculating() ||
                                  pathing.goal() != nullptr;
        if (renderActive) {
            visualizer_->update(pathing.currentPathPositions(), pathing.nextPathPositions(),
                                context_->eyePosition(), context_->position(),
                                context_->cameraDirection(), true,
                                fovCompensation_.load(std::memory_order_relaxed));
        } else {
            visualizer_->update({}, {}, context_->eyePosition(), context_->position(),
                                context_->cameraDirection(), false,
                                fovCompensation_.load(std::memory_order_relaxed));
        }
    }

    const auto& controls = lastControls_;
    auto& pathing = baritone_->pathing();
    const bool produced = pathing.producedOutput();
    const bool inputsChanged = !haveLastInputs_ || controls.inputs != lastInputs_;
    if (ticks_ <= 2 || produced != lastProduced_ || inputsChanged) {
        logger::info(
            "CONTROL tick={} produced={} status={} inputs='{}' mask={} target={} "
            "pathing={} calc={} inProgress={} calcFailed={} paused={} safeCancel={} "
            "goal='{}' mover='{}'",
            ticks_, produced, statusName(controls.status), inputNames(controls.inputs),
            controls.inputs.to_string(), rotationText(controls.target), pathing.isPathing(),
            pathing.isCalculating(), pathing.hasInProgress(), pathing.calcFailedLastTick(),
            pathing.pausedThisTick(), pathing.isSafeToCancel(), goalText(pathing.goal()),
            pathing.debugState());
        lastProduced_ = produced;
        lastInputs_ = controls.inputs;
        haveLastInputs_ = true;
    }

    if (ticks_ == 1 || ticks_ % 20 == 0) {
        const auto eta = pathing.estimatedTicksToGoal();
        const auto pathInfo = pathing.currentPathInfo();
        logger::info(
            "TICK {} entity=0x{:08X} actor={} bs={} reg=0x{:X} dim={} pos=({:.3f},{:.3f},{:.3f}) "
            "eye=({:.3f},{:.3f},{:.3f}) vel=({:.3f},{:.3f},{:.3f}) rot=(yaw {:.1f}, pitch "
            "{:.1f}) ground={} water={} sneak={} fly={} creative={} inputs='{}' status={} "
            "target={} pathing={} calc={} produced={} goal='{}' mover='{}' path={} eta={:.1f} "
            "chunks={} states={} scanned={} scan={:.2f}ms packs={} cache={}",
            ticks_, entity->raw, actorPtr, regionPtr, mem::addressOf(registry), dimension, pos.x,
            pos.y, pos.z, context_->eyePosition().x, context_->eyePosition().y,
            context_->eyePosition().z, context_->velocity().x, context_->velocity().y,
            context_->velocity().z, context_->rotation().yaw, context_->rotation().pitch,
            context_->onGround(), context_->isInWater(), context_->isSneaking(),
            context_->isFlying(), context_->isCreative(), inputNames(controls.inputs),
            statusName(controls.status), rotationText(controls.target), pathing.isPathing(),
            pathing.isCalculating(), produced, goalText(pathing.goal()), pathing.debugState(),
            pathInfoText(pathInfo), eta.value_or(-1.0), snapshot ? snapshot->chunkCount() : 0,
            tracker.knownStates(), tracker.scannedChunks(), tracker.lastChunkScanMs(),
            tracker.pendingPacks(), tracker.cacheActive());
    }
}

}  // namespace baritone::platform
