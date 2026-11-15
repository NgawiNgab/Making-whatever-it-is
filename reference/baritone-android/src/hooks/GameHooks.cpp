#include "hooks/GameHooks.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

#include <pl/Logger.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include "memory/Signatures.h"
#include "platform/BaritoneRuntime.h"
#include "platform/Log.h"
#include "sdk/Game.h"
#include "sdk/Memory.h"
#include "sdk/Offsets.h"

namespace baritone::hooks {
namespace {

namespace filelogger = ::logger;

using TickFn = void (*)(void*, void*, void*, void*, void*, void*, void*, void*);
using EnqueueDirectionFn = void (*)(void*, int, float, float, int, int, bool);

std::atomic<void*> g_registry{nullptr};
std::atomic<std::uint64_t> g_ticks{0};
std::atomic<bool> g_captureLogged{false};
TickFn g_originalTick = nullptr;
std::uintptr_t g_tickAddress = 0;

std::atomic<void*> g_inputHandler{nullptr};
std::atomic<void*> g_clientInstance{nullptr};
std::atomic<bool> g_pendingTurn{false};
std::atomic<float> g_pendingTurnX{0.0f};
std::atomic<float> g_pendingTurnY{0.0f};
std::atomic<int> g_pendingTurnFocus{2};
std::atomic<int> g_pendingTurnController{-1};
std::atomic<bool> g_directionMapLogged{false};
std::atomic<std::uint64_t> g_inputTicks{0};
TickFn g_originalInputTick = nullptr;
std::uintptr_t g_inputTickAddress = 0;
EnqueueDirectionFn g_enqueueDirection = nullptr;

void inputTickDetour(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6,
                     void* a7);

pl::log::Logger& logger() {
    static auto& instance = pl::log::Logger::getOrCreate("Baritone");
    return instance;
}

template <typename... Args>
void diagInfo(std::format_string<Args...> fmt, Args&&... args) {
    auto text = std::format(fmt, std::forward<Args>(args)...);
    filelogger::info("DIAG {}", text);
    logger().info("{}", text);
}

template <typename... Args>
void diagWarn(std::format_string<Args...> fmt, Args&&... args) {
    auto text = std::format(fmt, std::forward<Args>(args)...);
    filelogger::warn("DIAG {}", text);
    logger().warn("{}", text);
}

void logStorage(const char* name, sdk::GameSparseSet* storage) {
    diagInfo("  storage {:<20} = {}", name, static_cast<void*>(storage));
}

void logDiagnosticsImpl(void* registry) {
    sdk::GameRegistry reg(registry);
    if (!reg.valid()) {
        diagWarn("registry {} raw=0x{:X} is not a valid game registry", registry, mem::addressOf(registry));
        return;
    }

    const auto* base = static_cast<const std::uint8_t*>(registry);
    const auto sparseBegin = mem::read<const std::uint64_t*>(base, 0x60);
    const auto sparseEnd = mem::read<const std::uint64_t*>(base, 0x68);
    const auto buckets = (sparseBegin && sparseEnd >= sparseBegin)
                             ? static_cast<std::size_t>(sparseEnd - sparseBegin)
                             : 0;

    diagInfo("tick {} registry {} raw=0x{:X} buckets {}", g_ticks.load(std::memory_order_relaxed),
             registry, mem::addressOf(registry), buckets);

    logStorage("StateVector", reg.storage(sdk::StateVectorComponent::kTypeHash));
    logStorage("AABBShape", reg.storage(sdk::AABBShapeComponent::kTypeHash));
    logStorage("ActorRotation", reg.storage(sdk::ActorRotationComponent::kTypeHash));
    logStorage("MoveInput", reg.storage(sdk::MoveInputComponent::kTypeHash));
    logStorage("MovementAbilities", reg.storage(sdk::MovementAbilitiesComponent::kTypeHash));
    logStorage("ActorOwner", reg.storage(sdk::ActorOwnerComponent::kTypeHash));
    logStorage("LocalPlayer", reg.storage(sdk::LocalPlayerComponent::kTypeHash));
    logStorage("Player", reg.storage(sdk::PlayerComponent::kTypeHash));

    auto entity = reg.localPlayerEntity();
    if (!entity) diagInfo("  no LocalPlayerComponent entity, trying PlayerComponent");
    if (!entity || !reg.actorFor(*entity)) {
        if (auto fallback = reg.playerEntity()) entity = fallback;
    }
    if (!entity) {
        diagInfo("  no player entity with an actor owner");
        return;
    }

    void* actorPtr = reg.actorFor(*entity);
    diagInfo("  localPlayer entity=0x{:08X} actor={} raw=0x{:X}", entity->raw, actorPtr,
             mem::addressOf(actorPtr));

    const auto* state = reg.tryGet<sdk::StateVectorComponent>(*entity);
    const auto* rotation = reg.tryGet<sdk::ActorRotationComponent>(*entity);
    const auto* aabb = reg.tryGet<sdk::AABBShapeComponent>(*entity);
    const auto* move = reg.tryGet<sdk::MoveInputComponent>(*entity);
    const auto* abilities = reg.tryGet<sdk::MovementAbilitiesComponent>(*entity);

    if (state) {
        diagInfo("  pos=({:.2f}, {:.2f}, {:.2f}) old=({:.2f}, {:.2f}, {:.2f}) "
                      "vel=({:.2f}, {:.2f}, {:.2f})",
                      state->pos.x, state->pos.y, state->pos.z,
                      state->posOld.x, state->posOld.y, state->posOld.z,
                      state->velocity.x, state->velocity.y, state->velocity.z);
    } else {
        diagInfo("  StateVectorComponent missing");
    }

    if (rotation) {
        diagInfo("  rotation pitch={:.2f} yaw={:.2f}", rotation->rotation.x,
                      rotation->rotation.y);
    }
    if (aabb) {
        diagInfo("  aabb min=({:.2f}, {:.2f}, {:.2f}) max=({:.2f}, {:.2f}, {:.2f}) "
                      "size=({:.2f}, {:.2f})",
                      aabb->box.min.x, aabb->box.min.y, aabb->box.min.z,
                      aabb->box.max.x, aabb->box.max.y, aabb->box.max.z, aabb->size.x,
                      aabb->size.y);
    }
    if (move) {
        diagInfo("  move=({:.2f}, {:.2f}) rawJump={} jump={} sneak={} sprint={}",
                      move->move.x, move->move.y, static_cast<bool>(move->rawInputState.jumpDown),
                      static_cast<bool>(move->jumping), static_cast<bool>(move->sneaking),
                      static_cast<bool>(move->sprinting));
    }
    if (abilities) {
        diagInfo("  flying={}",
                      (abilities->data[offsets::MovementAbilities_flags] & 1u) != 0);
    }

    auto* cameraTargetStorage = reg.storageByName("21CameraTargetComponent");
    auto* cameraStorage = reg.storageByName("15CameraComponent");
    auto* targetOrientationStorage = reg.storageByName("32TargetCameraOrientationComponent");
    auto* activeCameraStorage = reg.storageByName("21ActiveCameraComponent");
    diagInfo("  camera storages target={} camera={} targetOrient={} active={}",
             static_cast<void*>(cameraTargetStorage), static_cast<void*>(cameraStorage),
             static_cast<void*>(targetOrientationStorage),
             static_cast<void*>(activeCameraStorage));

    const auto logCameraEntity = [&](EntityId entity, const char* tag) {
        const auto* target =
            cameraTargetStorage && cameraTargetStorage->contains(entity)
                ? static_cast<const sdk::CameraTargetComponent*>(
                      cameraTargetStorage->value(entity))
                : nullptr;
        const auto* camera =
            cameraStorage && cameraStorage->contains(entity)
                ? static_cast<const sdk::CameraComponent*>(cameraStorage->value(entity))
                : nullptr;
        const auto* orientation =
            targetOrientationStorage && targetOrientationStorage->contains(entity)
                ? static_cast<const sdk::TargetCameraOrientationComponent*>(
                      targetOrientationStorage->value(entity))
                : nullptr;

        diagInfo("  camera[{}] entity=0x{:08X} target={} camera={} orientation={}", tag,
                 entity.raw, static_cast<const void*>(target),
                 static_cast<const void*>(camera), static_cast<const void*>(orientation));
        if (target) {
            diagInfo("    targetRot=({:.3f},{:.3f}) curOrient=({:.3f},{:.3f},{:.3f}) "
                     "initialRot=({:.3f},{:.3f})",
                     target->currentRotation.x, target->currentRotation.y,
                     target->currentCameraOrientation.x, target->currentCameraOrientation.y,
                     target->currentCameraOrientation.z, target->initialRotation.x,
                     target->initialRotation.y);
        }
        if (camera) {
            const auto idHash = mem::read<std::uint64_t>(camera, 0);
            diagInfo("    camera idHash=0x{:016X} pos=({:.2f},{:.2f},{:.2f}) "
                     "quat=({:.3f},{:.3f},{:.3f},{:.3f})",
                     idHash, camera->position.x, camera->position.y, camera->position.z,
                     camera->orientation.x, camera->orientation.y, camera->orientation.z,
                     camera->orientation.w);
        }
        if (orientation) {
            diagInfo("    targetQuat=({:.3f},{:.3f},{:.3f},{:.3f})",
                     orientation->orientation.x, orientation->orientation.y,
                     orientation->orientation.z, orientation->orientation.w);
        }
    };

    if (activeCameraStorage) {
        for (auto it = activeCameraStorage->begin(); it != activeCameraStorage->end(); ++it) {
            const auto cameraEntity = *it;
            if (cameraEntity == entt::tombstone || cameraEntity == entt::null) continue;
            logCameraEntity(cameraEntity, "active");
        }
    }
    if (cameraTargetStorage) {
        std::size_t logged = 0;
        for (auto it = cameraTargetStorage->begin();
             it != cameraTargetStorage->end() && logged < 4; ++it) {
            const auto cameraEntity = *it;
            if (cameraEntity == entt::tombstone || cameraEntity == entt::null) continue;
            logCameraEntity(cameraEntity, "target");
            ++logged;
        }
    }
    if (cameraTargetStorage && cameraTargetStorage->contains(*entity)) {
        logCameraEntity(*entity, "player");
    }
    if (cameraStorage) {
        diagInfo("  camera component storage size={}", cameraStorage->size());
        std::size_t logged = 0;
        for (auto it = cameraStorage->begin();
             it != cameraStorage->end() && logged < 8; ++it) {
            const auto cameraEntity = *it;
            if (cameraEntity == entt::tombstone || cameraEntity == entt::null) continue;
            logCameraEntity(cameraEntity, "all");
            ++logged;
        }
    }

    if (!actorPtr || !mem::isReadable(actorPtr, 0x1D0)) {
        diagWarn("  actor pointer is not readable");
        return;
    }

    sdk::Actor actor(actorPtr);
    if (auto* context = actor.entityContext(); context && mem::isReadable(context, sizeof(*context))) {
        diagInfo("  context registry={} entity=0x{:08X} matchesTick={}", context->registry,
                      context->entity.raw, context->registry == registry);
    }

    mem::Range module{};
    const bool haveModule = mem::moduleRange("libminecraftpe.so", module);
    const auto inModule = [&](const void* ptr) {
        if (!ptr || !haveModule) return false;
        const auto value = mem::addressOf(ptr);
        return value >= module.start && value < module.end;
    };

    constexpr std::ptrdiff_t candidates[] = {0x1C0, 0x1F0, 0x1D0, 0x1E0, 0x1B0,
                                             0x200, 0x1B8, 0x1C8, 0x1D8, 0x1E8,
                                             0x1F8, 0x208, 0x210};
    void* blockSource = nullptr;
    std::ptrdiff_t dimensionOffset = 0;

    for (const auto offset : candidates) {
        auto* dimension = mem::read<void*>(actorPtr, static_cast<std::size_t>(offset));
        if (!mem::isReadable(dimension, 0x100)) continue;

        auto* dimensionVtable = mem::read<void*>(dimension, 0);
        auto* candidate = mem::read<void*>(dimension, offsets::Dimension_blockSource);
        if (!mem::isReadable(candidate, 0x100)) continue;

        auto* candidateVtable = mem::read<void*>(candidate, 0);
        if (!inModule(candidateVtable)) continue;

        diagInfo("  dimension candidate +0x{:X}: dim={} dimVt={} blockSource={} bsVt={}",
                      offset, dimension, dimensionVtable, candidate, candidateVtable);
        if (!blockSource) {
            blockSource = candidate;
            dimensionOffset = offset;
        }
    }

    if (!blockSource) {
        diagWarn("  no dimension/block source candidate found");
        return;
    }

    diagInfo("  using dimension offset +0x{:X}", dimensionOffset);

    auto** blockSourceVtable = *reinterpret_cast<void***>(blockSource);
    if (!inModule(blockSourceVtable) || !inModule(blockSourceVtable[18])) {
        diagWarn("  block source vtable is not usable");
        return;
    }

    sdk::BlockSource source(blockSource);
    const auto feet = sdk::BlockPos::floor(state ? state->pos : sdk::Vec3{});
    const auto* block = source.getBlock(feet);
    const auto* below = source.getBlock(feet.below());
    const sdk::Block blockView(block);
    const sdk::Block belowView(below);

    diagInfo("  feet=({}, {}, {}) block={} name='{}' legacy='{}'", feet.x, feet.y, feet.z, block,
                  blockView.name(), blockView.legacyName());
    diagInfo("  below=({}, {}, {}) block={} name='{}' legacy='{}'", feet.x, feet.y - 1,
                  feet.z, below, belowView.name(), belowView.legacyName());
}

bool installInputHook() {
    if (g_originalInputTick) return true;
    if (!g_inputTickAddress) return false;

    filelogger::warn("hooking InputHandler::tick at 0x{:X}", g_inputTickAddress);
    void* inputOriginal = nullptr;
    if (pl::memory::hook(reinterpret_cast<void*>(g_inputTickAddress),
                         reinterpret_cast<void*>(&inputTickDetour), &inputOriginal) != 0) {
        filelogger::error("failed to hook InputHandler::tick at 0x{:X}", g_inputTickAddress);
        return false;
    }
    g_originalInputTick = reinterpret_cast<TickFn>(inputOriginal);
    filelogger::warn("hooked InputHandler::tick at 0x{:X} (raw=0x{:X})", g_inputTickAddress,
                     g_inputTickAddress);
    return true;
}

void tickDetour(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7) {
    if (a1) {
        if (auto* registry = *static_cast<void**>(a1)) {
            g_registry.store(registry, std::memory_order_relaxed);
        }
    }

    const auto registry = g_registry.load(std::memory_order_relaxed);
    if (a1 && !g_captureLogged.exchange(true, std::memory_order_relaxed)) {
        void* viewRegistry = *static_cast<void**>(a1);
        void* view8 = mem::read<void*>(a1, 8);
        filelogger::info(
            "TICK capture a0={} a1={} viewRegistry={} viewRaw=0x{:X} view8={} view8Raw=0x{:X} "
            "registry={} registryRaw=0x{:X}",
            a0, a1, viewRegistry, mem::addressOf(viewRegistry), view8, mem::addressOf(view8),
            registry, mem::addressOf(registry));
    }
    if (registry) platform::BaritoneRuntime::get().beforeGameTick(registry);

    if (g_ticks.load(std::memory_order_relaxed) >= 40 && !g_originalInputTick) {
        installInputHook();
    }

    if (g_originalTick) {
        g_originalTick(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    if (registry) platform::BaritoneRuntime::get().afterGameTick(registry);
    if (!registry) return;

    const auto tick = g_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (tick == 1 || (tick % 20) == 0) {
        logDiagnosticsImpl(registry);
    }
}

void dumpDirectionMap(void* ih) {
    if (!ih || !mem::isReadable(ih, 0x2d0)) return;

    const auto begin = mem::read<void*>(ih, 0xb8);
    const auto end = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(ih) + 0xc8);
    filelogger::info("INPUT direction map ih={} begin={} end={}", ih, begin, end);
    if (!begin || begin == end || !mem::isReadable(begin, 0x60)) return;

    void* stack[64];
    int depth = 0;
    void* node = begin;
    int visited = 0;

    while ((node && node != end) || depth > 0) {
        while (node && node != end) {
            if (depth < 64) stack[depth++] = node;
            node = mem::read<void*>(node, 0x00);
        }
        if (depth == 0) break;
        node = stack[--depth];
        if (!mem::isReadable(node, 0x60)) break;

        const auto key = mem::read<std::uint8_t>(node, 0x20);
        const auto q28 = mem::read<void*>(node, 0x28);
        const auto q30 = mem::read<void*>(node, 0x30);
        const auto q38 = mem::read<void*>(node, 0x38);
        const auto q40 = mem::read<void*>(node, 0x40);
        const auto q48 = mem::read<void*>(node, 0x48);
        const auto q50 = mem::read<void*>(node, 0x50);
        const auto q58 = mem::read<void*>(node, 0x58);
        filelogger::info(
            "INPUT direction key={} node={} q28={} q30={} q38={} q40={} q48={} q50={} q58={}",
            key, node, q28, q30, q38, q40, q48, q50, q58);

        if (q50 && mem::isReadable(q50, 0x38)) {
            const auto vt = mem::read<void*>(q50, 0x00);
            const auto invoker =
                (vt && mem::isReadable(vt, 0x38)) ? mem::read<void*>(vt, 0x30) : nullptr;
            filelogger::info("  q50 vt={} invoker={}", vt, invoker);
        }

        node = mem::read<void*>(node, 0x08);
        if (++visited > 64) break;
    }
}

void inputTickDetour(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6,
                     void* a7) {
    if (a0) g_inputHandler.store(a0, std::memory_order_relaxed);
    if (a2) g_clientInstance.store(a2, std::memory_order_relaxed);

    if (a0 && g_enqueueDirection && mem::isReadable(a0, 0x2d0)) {
        if (g_pendingTurn.exchange(false, std::memory_order_relaxed)) {
            void* queue = mem::read<void*>(a0, 0x260);
            if (queue && mem::isReadable(queue, 0x30)) {
                const float x = g_pendingTurnX.load(std::memory_order_relaxed);
                const float y = g_pendingTurnY.load(std::memory_order_relaxed);
                const int focus = g_pendingTurnFocus.load(std::memory_order_relaxed);
                const int controller = g_pendingTurnController.load(std::memory_order_relaxed);
                filelogger::warn("INPUT enqueue begin queue={} x={:.4f} y={:.4f}", queue, x, y);
                g_enqueueDirection(queue, 1, x, y, focus, controller, false);
                filelogger::warn("INPUT enqueue done");
            }
        }
    }

    if (g_originalInputTick) {
        g_originalInputTick(a0, a1, a2, a3, a4, a5, a6, a7);
    }
}

}  // namespace

bool install() {
    if (g_originalTick) return true;

    mem::Range module{};
    if (mem::moduleRange(signatures::moduleName, module)) {
        filelogger::info("module {} range 0x{:X}..0x{:X}", signatures::moduleName, module.start,
                         module.end);
    } else {
        filelogger::warn("could not resolve module range for {}", signatures::moduleName);
    }

    const auto getBlockAddress =
        pl::memory::resolveSignature(signatures::BlockSourceGetBlock, signatures::moduleName);
    if (getBlockAddress) {
        sdk::gameFunctions().getBlock =
            reinterpret_cast<const void* (*)(void*, const sdk::BlockPos*)>(getBlockAddress);
        filelogger::info("resolved BlockSource::getBlock at 0x{:X}", getBlockAddress);
    } else {
        filelogger::warn("failed to resolve BlockSource::getBlock");
    }

    const auto setRotationAddress =
        pl::memory::resolveSignature(signatures::ActorSetRotation, signatures::moduleName);
    if (setRotationAddress) {
        sdk::gameFunctions().setRotation =
            reinterpret_cast<void (*)(void*, const sdk::Vec2*)>(setRotationAddress);
        filelogger::info("resolved Actor::setRotation at 0x{:X}", setRotationAddress);
    } else {
        filelogger::warn("failed to resolve Actor::setRotation");
    }

    const auto enqueueAddress = pl::memory::resolveSignature(
        signatures::InputEventQueue_enqueueDirection, signatures::moduleName);
    if (enqueueAddress) {
        g_enqueueDirection = reinterpret_cast<EnqueueDirectionFn>(enqueueAddress);
        filelogger::info("resolved InputEventQueue::enqueueDirection at 0x{:X}", enqueueAddress);
    } else {
        filelogger::warn("failed to resolve InputEventQueue::enqueueDirection");
    }

    const auto address = pl::memory::resolveSignature(
        signatures::ClientInputUpdateSystemInternal_tick, signatures::moduleName);
    if (!address) {
        filelogger::error("failed to resolve ClientInputUpdateSystemInternal::tick");
        return false;
    }

    void* original = nullptr;
    if (pl::memory::hook(reinterpret_cast<void*>(address),
                         reinterpret_cast<void*>(&tickDetour), &original) != 0) {
        filelogger::error("failed to hook ClientInputUpdateSystemInternal::tick at 0x{:X}",
                          address);
        return false;
    }

    g_originalTick = reinterpret_cast<TickFn>(original);
    g_tickAddress = address;
    filelogger::info("hooked ClientInputUpdateSystemInternal::tick at 0x{:X} (raw=0x{:X})",
                     address, address);

    const auto inputTickAddress =
        pl::memory::resolveSignature(signatures::InputHandler_tick, signatures::moduleName);
    if (!inputTickAddress) {
        filelogger::error("failed to resolve InputHandler::tick");
        return false;
    }

    g_inputTickAddress = inputTickAddress;
    filelogger::info("resolved InputHandler::tick at 0x{:X} (hook deferred)", inputTickAddress);

    return true;
}

void uninstall() {
    if (g_originalInputTick) {
        pl::memory::unhook(reinterpret_cast<void*>(g_inputTickAddress),
                           reinterpret_cast<void*>(&inputTickDetour));
        g_originalInputTick = nullptr;
        g_inputTickAddress = 0;
        g_inputHandler.store(nullptr, std::memory_order_relaxed);
        g_clientInstance.store(nullptr, std::memory_order_relaxed);
        filelogger::info("unhooked InputHandler::tick");
    }

    if (!g_originalTick) return;

    pl::memory::unhook(reinterpret_cast<void*>(g_tickAddress),
                       reinterpret_cast<void*>(&tickDetour));
    g_originalTick = nullptr;
    g_tickAddress = 0;
    g_registry.store(nullptr, std::memory_order_relaxed);
    filelogger::info("unhooked ClientInputUpdateSystemInternal::tick");
}

void* registry() {
    return g_registry.load(std::memory_order_relaxed);
}

std::uint64_t tickCount() {
    return g_ticks.load(std::memory_order_relaxed);
}

void* inputHandler() {
    return g_inputHandler.load(std::memory_order_relaxed);
}

void* clientInstance() {
    return g_clientInstance.load(std::memory_order_relaxed);
}

void setPendingTurn(float x, float y, int focusImpact, int controllerId) {
    g_pendingTurnX.store(x, std::memory_order_relaxed);
    g_pendingTurnY.store(y, std::memory_order_relaxed);
    g_pendingTurnFocus.store(focusImpact, std::memory_order_relaxed);
    g_pendingTurnController.store(controllerId, std::memory_order_relaxed);
    g_pendingTurn.store(true, std::memory_order_relaxed);
}

void logDiagnostics(void* registry) {
    logDiagnosticsImpl(registry);
}

}  // namespace baritone::hooks
