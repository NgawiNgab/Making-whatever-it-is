#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sdk/Components.h"
#include "sdk/Memory.h"
#include "sdk/Math.h"
#include "sdk/Offsets.h"

namespace sdk {

// The game's EntityContext, stored at Actor+0x8.
struct GameEntityContext {
    void* owner;      // EntityRegistry&
    void* registry;   // entt::basic_registry<EntityId>&
    EntityId entity;  // this actor's entity
};
static_assert(offsetof(GameEntityContext, registry) == 0x8);
static_assert(offsetof(GameEntityContext, entity) == 0x10);

struct GameFunctions {
    const void* (*getBlock)(void* blockSource, const BlockPos* pos) = nullptr;
    void (*setRotation)(void* actor, const Vec2* rotation) = nullptr;
};

GameFunctions& gameFunctions();

class GameRegistry {
public:
    explicit GameRegistry(void* registry) : registry_(registry) {}

    [[nodiscard]] bool valid() const;
    [[nodiscard]] void* raw() const { return registry_; }

    [[nodiscard]] GameSparseSet* storage(uint32_t hash) const;
    // Finds a storage by a substring of its RTTI type name, e.g. "CameraComponent".
    [[nodiscard]] GameSparseSet* storageByName(std::string_view needle) const;
    [[nodiscard]] bool has(EntityId entity, uint32_t hash) const;
    [[nodiscard]] const void* rawComponent(EntityId entity, uint32_t hash) const;

    template <GameComponentType T>
    [[nodiscard]] T* tryGet(EntityId entity) const {
        auto* set = storage(T::kTypeHash);
        if (!set || !set->contains(entity)) return nullptr;
        return const_cast<T*>(static_cast<const T*>(set->value(entity)));
    }

    [[nodiscard]] std::vector<EntityId> entities(uint32_t hash) const;
    [[nodiscard]] std::optional<EntityId> firstEntity(uint32_t hash) const;
    [[nodiscard]] std::optional<EntityId> localPlayerEntity() const;
    // LocalPlayerComponent, falling back to a PlayerComponent entity that owns an actor.
    [[nodiscard]] std::optional<EntityId> playerEntity() const;
    [[nodiscard]] void* actorFor(EntityId entity) const;

private:
    void* registry_;
};

class BlockSource {
public:
    explicit BlockSource(void* ptr) : ptr_(ptr) {}
    [[nodiscard]] void* raw() const { return ptr_; }

    [[nodiscard]] const void* getBlock(const BlockPos& pos) const {
        auto& functions = gameFunctions();
        return (ptr_ && functions.getBlock) ? functions.getBlock(ptr_, &pos) : nullptr;
    }

    [[nodiscard]] int dimensionId() const;

private:
    void* ptr_;
};

class Block {
public:
    explicit Block(const void* ptr) : ptr_(ptr) {}
    [[nodiscard]] const void* raw() const { return ptr_; }

    // "minecraft:stone" through BlockType::mNameInfo -> NameInfo::mFullName.
    [[nodiscard]] std::string name() const;
    // Fallback used by the 26.63 Windows port: BlockLegacy::mNamespacedId.
    [[nodiscard]] std::string legacyName() const;

private:
    const void* ptr_;
};

class Actor {
public:
    explicit Actor(void* ptr) : ptr_(ptr) {}
    [[nodiscard]] void* raw() const { return ptr_; }

    [[nodiscard]] GameEntityContext* entityContext() const {
        return ptr_ ? mem::member<GameEntityContext>(ptr_, offsets::Actor_entityContext) : nullptr;
    }

    [[nodiscard]] void* dimension() const {
        return ptr_ ? mem::read<void*>(ptr_, offsets::Actor_dimension) : nullptr;
    }

    [[nodiscard]] void* blockSource(bool verbose = false) const;

private:
    void* ptr_;
};

}  // namespace sdk
