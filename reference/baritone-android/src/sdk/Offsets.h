#pragma once
#include <cstddef>
#include <cstdint>

// Member offsets and vtable indices recovered from Minecraft Bedrock 1.26.62.1.
// Keep every game-specific constant here so a version bump has one place to fix.
namespace offsets {

// Actor/EntityContext: EntityRegistry&, entt::basic_registry<EntityId>&, EntityId
inline constexpr std::ptrdiff_t Actor_entityContext = 0x8;
inline constexpr std::ptrdiff_t EntityContext_registry = 0x8;
inline constexpr std::ptrdiff_t EntityContext_entity = 0x10;

inline constexpr std::ptrdiff_t Actor_dimension = 0x1C0;  // Dimension* (raw)
inline constexpr std::ptrdiff_t Dimension_blockSource = 0xD0;  // BlockSource*

inline constexpr std::ptrdiff_t MovementAbilities_flags = 0x4;  // bit 0: flying

inline constexpr std::ptrdiff_t Block_blockType = 0x68;  // BlockType*
inline constexpr std::ptrdiff_t BlockType_nameInfo = 0x88;
inline constexpr std::ptrdiff_t NameInfo_fullName = 0x40;
inline constexpr std::ptrdiff_t HashedString_string = 0x8;

// Alternative name path used by the 26.63 Windows port (BlockLegacy::mNamespacedId).
inline constexpr std::ptrdiff_t BlockLegacy_namespacedId = 0xE0;

inline constexpr std::size_t BlockSource_vt_getDimensionId = 18;

// Actor::setRotation(Vec2 const&), the same setter TeleportCommand uses for /tp ... facing.

inline constexpr uint32_t ActorType_Player = 319;

}  // namespace offsets
