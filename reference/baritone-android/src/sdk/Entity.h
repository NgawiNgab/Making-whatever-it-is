#pragma once
// Access to the game's EnTT registry. Bedrock stores most actor state in ECS components.
// The traits below reproduce how the game instantiates EnTT so that the registry and
// storage layouts match: 18-bit entity index, custom component hashes, storages
// without signal mixins.
#include <entt/entity/sparse_set.hpp>

#include <cstdint>
#include <string_view>
#include <type_traits>

struct EntityId {
    uint32_t raw;

    constexpr bool operator==(const EntityId&) const = default;
    constexpr operator uint32_t() const { return raw; }
};

struct EntityIdTraits {
    using value_type = EntityId;
    using entity_type = uint32_t;
    using version_type = uint16_t;

    static constexpr entity_type entity_mask = 0x3FFFF;
    static constexpr entity_type version_mask = 0x3FFF;
};

template <>
struct entt::entt_traits<EntityId> : entt::basic_entt_traits<EntityIdTraits> {
    static constexpr std::size_t page_size = 2048;
};

// The game hashes component types by name: FNV-1a 32 of "StateVectorComponent" etc.
constexpr uint32_t componentHash(std::string_view name) {
    uint32_t h = 0x811C9DC5;
    for (char c : name) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x01000193;
    }
    return h;
}
static_assert(componentHash("StateVectorComponent") == 0x1B5D5238);

// Every game component we read derives from this and declares the hash the game uses.
struct GameComponent {};

template <typename T>
concept GameComponentType = std::derived_from<T, GameComponent>;

template <GameComponentType Type>
struct entt::component_traits<Type, EntityId> {
    using element_type = Type;
    using entity_type = EntityId;
    static constexpr bool in_place_delete = true;
    static constexpr std::size_t page_size = 128 * !std::is_empty_v<Type>;
};

template <typename Type>
struct entt::storage_type<Type, EntityId> {
    using type = basic_storage<Type, EntityId>;
};

template <GameComponentType Type>
struct entt::type_hash<Type> {
    [[nodiscard]] static consteval id_type value() noexcept { return Type::kTypeHash; }
    [[nodiscard]] consteval operator id_type() const noexcept { return value(); }
};

namespace sdk {

// The game's storages derive from basic_sparse_set<EntityId>. Casting the storage pointer
// to this type is safe: the base subobject is at offset zero and the game uses the same
// EnTT revision. value()/contains() then dispatch through the game's own get_at.
using GameSparseSet = entt::basic_sparse_set<EntityId>;

}  // namespace sdk
