#include "GameHooks.hpp"

#include "RotationSource.hpp"

#include <pl/Logger.hpp>
#include <pl/memory/Hook.hpp>

#include <atomic>

namespace camface::game {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

// BedrockTools src/core/GameHooks.cpp:35 -- the update function's real signature.
using ClientInstanceUpdateFn = void* (*)(void*, bool);
// src/core/GameHooks.cpp:133 -- tickDetour takes a single actor pointer.
using NormalTickFn = void (*)(void*);

ClientInstanceUpdateFn g_clientUpdateOriginal = nullptr;
NormalTickFn g_normalTickOriginal = nullptr;
RotationSource* g_rotation = nullptr;

void* g_clientUpdateTarget = nullptr;
void* g_normalTickTarget = nullptr;

std::atomic_bool g_tickSeen{false};

constexpr std::size_t kMinPatchWidth = 12;

// The game calls this on its ClientInstance (arg 0). Capturing it is the whole
// point: everything downstream (local player, rotation) hangs off this pointer.
void* clientUpdateDetour(void* clientInstance, bool value) {
    if (clientInstance != nullptr && g_rotation != nullptr) {
        g_rotation->setClientInstance(clientInstance);
    }
    if (g_clientUpdateOriginal == nullptr) return nullptr;
    return g_clientUpdateOriginal(clientInstance, value);
}

// Per-actor tick. Running *after* the original means the rotation component has
// already been written for this frame, so a read here is current rather than stale.
void normalTickDetour(void* actor) {
    if (g_normalTickOriginal != nullptr) g_normalTickOriginal(actor);

    g_tickSeen.store(true, std::memory_order_release);
    if (g_rotation != nullptr) {
        g_rotation->noteTick();
        // The local-player pointer is fetched from the client instance, not from
        // `actor`: the tick fires for every entity, and only the client knows which
        // one the player controls.
        g_rotation->refreshLocalPlayer();
    }
}

bool lengthOk(std::size_t measured) {
    return measured > kMinPatchWidth;
}

}  // namespace

GameHooks::Result GameHooks::install(Targets const& targets, RotationSource* rotation) {
    if (targets.clientInstanceUpdate == 0 || targets.normalTick == 0) {
        return Result::FactUnavailable;
    }
    if (!lengthOk(targets.clientInstanceUpdateSize) || !lengthOk(targets.normalTickSize)) {
        log().warn("hooks refused: measured length below patch width ({} / {})",
                   targets.clientInstanceUpdateSize, targets.normalTickSize);
        return Result::TargetTooSmall;
    }

    g_rotation = rotation;

    void* clientTarget = reinterpret_cast<void*>(targets.clientInstanceUpdate);
    if (pl::memory::hook(clientTarget, reinterpret_cast<void*>(&clientUpdateDetour),
                         reinterpret_cast<void**>(&g_clientUpdateOriginal)) != 0) {
        g_rotation = nullptr;
        g_clientUpdateOriginal = nullptr;
        return Result::EngineRefused;
    }
    g_clientUpdateTarget = clientTarget;

    void* tickTarget = reinterpret_cast<void*>(targets.normalTick);
    if (pl::memory::hook(tickTarget, reinterpret_cast<void*>(&normalTickDetour),
                         reinterpret_cast<void**>(&g_normalTickOriginal)) != 0) {
        // Undo the first hook rather than leaving a half-installed pair.
        pl::memory::unhook(clientTarget, reinterpret_cast<void*>(&clientUpdateDetour));
        g_clientUpdateTarget = nullptr;
        g_clientUpdateOriginal = nullptr;
        g_rotation = nullptr;
        return Result::EngineRefused;
    }
    g_normalTickTarget = tickTarget;

    log().info("hooks installed: ClientInstanceUpdate=0x{:x} NormalTick=0x{:x}",
               targets.clientInstanceUpdate, targets.normalTick);
    return Result::Installed;
}

void GameHooks::remove() {
    g_rotation = nullptr;
    if (g_normalTickTarget != nullptr) {
        pl::memory::unhook(g_normalTickTarget, reinterpret_cast<void*>(&normalTickDetour));
        g_normalTickTarget = nullptr;
        g_normalTickOriginal = nullptr;
    }
    if (g_clientUpdateTarget != nullptr) {
        pl::memory::unhook(g_clientUpdateTarget, reinterpret_cast<void*>(&clientUpdateDetour));
        g_clientUpdateTarget = nullptr;
        g_clientUpdateOriginal = nullptr;
    }
}

bool GameHooks::clientCaptured() { return g_rotation != nullptr; }

bool GameHooks::tickObserved() { return g_tickSeen.load(std::memory_order_acquire); }

}  // namespace camface::game
