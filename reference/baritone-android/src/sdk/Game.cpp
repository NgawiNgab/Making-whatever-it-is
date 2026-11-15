#include "sdk/Game.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <typeinfo>

#include "platform/Log.h"

namespace sdk {

GameFunctions& gameFunctions() {
    static GameFunctions functions;
    return functions;
}

bool GameRegistry::valid() const {
    if (!registry_ || !mem::isReadable(registry_, 0x90)) return false;

    const auto* base = static_cast<const std::uint8_t*>(registry_);
    const auto sparseBegin = mem::read<const std::uint64_t*>(base, 0x60);
    const auto sparseEnd = mem::read<const std::uint64_t*>(base, 0x68);
    const auto packedBegin = mem::read<const std::uint64_t*>(base, 0x78);
    const auto packedEnd = mem::read<const std::uint64_t*>(base, 0x80);

    if (!sparseBegin || !sparseEnd || !packedBegin || !packedEnd) return false;
    if (sparseEnd < sparseBegin || packedEnd < packedBegin) return false;
    if (!mem::isReadable(sparseBegin, (sparseEnd - sparseBegin) * sizeof(std::uint64_t))) return false;
    if (!mem::isReadable(packedBegin, (packedEnd - packedBegin) * sizeof(std::uint64_t))) return false;

    const auto buckets = static_cast<std::size_t>(sparseEnd - sparseBegin);
    if (buckets == 0 || buckets > (1u << 20)) return false;
    if ((buckets & (buckets - 1)) != 0) return false;
    return true;
}

GameSparseSet* GameRegistry::storage(uint32_t hash) const {
    if (!registry_) return nullptr;
    const auto* base = static_cast<const std::uint8_t*>(registry_);

    const auto sparseBegin = mem::read<const std::uint64_t*>(base, 0x60);
    const auto sparseEnd = mem::read<const std::uint64_t*>(base, 0x68);
    const auto packedBegin = mem::read<const std::uint64_t*>(base, 0x78);
    const auto packedEnd = mem::read<const std::uint64_t*>(base, 0x80);

    if (!sparseBegin || !sparseEnd || !packedBegin || !packedEnd) return nullptr;
    if (sparseEnd < sparseBegin || packedEnd < packedBegin) return nullptr;

    const auto buckets = static_cast<std::size_t>(sparseEnd - sparseBegin);
    if (buckets == 0 || (buckets & (buckets - 1)) != 0) return nullptr;

    auto index = sparseBegin[hash & (buckets - 1)];
    const auto nodeCount = static_cast<std::size_t>(packedEnd - packedBegin);
    for (std::size_t guard = 0; index != ~std::uint64_t{0} && guard < nodeCount; ++guard) {
        if (index >= nodeCount) return nullptr;
        const auto* node = packedBegin + index * 4;
        if (mem::read<std::uint32_t>(node, 0x8) == hash) {
            return mem::read<GameSparseSet*>(node, 0x10);
        }
        index = mem::read<std::uint64_t>(node, 0x0);
    }
    return nullptr;
}

GameSparseSet* GameRegistry::storageByName(std::string_view needle) const {
    if (!registry_) return nullptr;
    const auto* base = static_cast<const std::uint8_t*>(registry_);

    const auto sparseBegin = mem::read<const std::uint64_t*>(base, 0x60);
    const auto sparseEnd = mem::read<const std::uint64_t*>(base, 0x68);
    const auto packedBegin = mem::read<const std::uint64_t*>(base, 0x78);
    const auto packedEnd = mem::read<const std::uint64_t*>(base, 0x80);

    if (!sparseBegin || !sparseEnd || !packedBegin || !packedEnd) return nullptr;
    if (sparseEnd < sparseBegin || packedEnd < packedBegin) return nullptr;

    const auto nodeCount = static_cast<std::size_t>(packedEnd - packedBegin);
    for (std::size_t i = 0; i < nodeCount; ++i) {
        const auto* node = packedBegin + i * 4;
        auto* set = mem::read<GameSparseSet*>(node, 0x10);
        if (!set || !mem::isReadable(set, sizeof(void*))) continue;
        const auto* vtable = *reinterpret_cast<const void* const*>(set);
        if (!vtable) continue;
        const auto* typeinfoPtr = reinterpret_cast<const void* const*>(
            reinterpret_cast<const std::uint8_t*>(vtable) - sizeof(void*));
        if (!mem::isReadable(typeinfoPtr, sizeof(void*))) continue;
        const auto* typeinfo = *typeinfoPtr;
        if (!typeinfo || !mem::isReadable(typeinfo, sizeof(void*))) continue;
        const char* name = typeid(*set).name();
        if (name && std::string_view(name).find(needle) != std::string_view::npos) {
            return set;
        }
    }
    return nullptr;
}

bool GameRegistry::has(EntityId entity, uint32_t hash) const {
    auto* set = storage(hash);
    return set && set->contains(entity);
}

const void* GameRegistry::rawComponent(EntityId entity, uint32_t hash) const {
    auto* set = storage(hash);
    if (!set || !set->contains(entity)) return nullptr;
    return set->value(entity);
}

std::vector<EntityId> GameRegistry::entities(uint32_t hash) const {
    std::vector<EntityId> result;
    auto* set = storage(hash);
    if (!set) return result;

    for (auto it = set->begin(); it != set->end(); ++it) {
        const auto entity = *it;
        if (entity == entt::tombstone || entity == entt::null) continue;
        result.push_back(entity);
    }
    return result;
}

std::optional<EntityId> GameRegistry::firstEntity(uint32_t hash) const {
    auto* set = storage(hash);
    if (!set) return std::nullopt;

    for (auto it = set->begin(); it != set->end(); ++it) {
        const auto entity = *it;
        if (entity == entt::tombstone || entity == entt::null) continue;
        return entity;
    }
    return std::nullopt;
}

std::optional<EntityId> GameRegistry::localPlayerEntity() const {
    return firstEntity(LocalPlayerComponent::kTypeHash);
}

std::optional<EntityId> GameRegistry::playerEntity() const {
    auto entity = localPlayerEntity();
    if (entity && actorFor(*entity)) return entity;
    for (const auto candidate : entities(PlayerComponent::kTypeHash)) {
        if (actorFor(candidate)) return candidate;
    }
    return std::nullopt;
}

void* GameRegistry::actorFor(EntityId entity) const {
    const auto* value = static_cast<void* const*>(rawComponent(entity, ActorOwnerComponent::kTypeHash));
    return value ? *value : nullptr;
}

int BlockSource::dimensionId() const {
    if (!ptr_) return 0;
    auto** vtable = *reinterpret_cast<void***>(ptr_);
    if (!vtable || !mem::isReadable(vtable, 20 * sizeof(void*))) return 0;
    auto fn = reinterpret_cast<int (*)(void*)>(vtable[offsets::BlockSource_vt_getDimensionId]);
    return fn ? fn(ptr_) : 0;
}

namespace {

bool inGameModule(const void* ptr) {
    static mem::Range range{};
    static const bool haveRange = mem::moduleRange("libminecraftpe.so", range);
    if (!ptr || !haveRange) return false;
    const auto value = mem::addressOf(ptr);
    return value >= range.start && value < range.end;
}

bool validBlockSource(const void* ptr) {
    if (!ptr || !mem::isReadable(ptr, 0x100)) return false;
    auto** vtable = *reinterpret_cast<void***>(const_cast<void*>(ptr));
    if (!vtable || !mem::isReadable(vtable, 20 * sizeof(void*))) return false;
    return inGameModule(vtable) && inGameModule(vtable[offsets::BlockSource_vt_getDimensionId]);
}

}  // namespace

void* Actor::blockSource(bool verbose) const {
    if (!ptr_ || !mem::isReadable(ptr_, 0x220)) {
        if (verbose) logger::warn("BLOCKSOURCE actor={} not readable (0x220)", ptr_);
        return nullptr;
    }

    auto* dim = mem::read<void*>(ptr_, offsets::Actor_dimension);
    const bool dimReadable =
        dim && mem::isReadable(dim, offsets::Dimension_blockSource + sizeof(void*));
    auto* primaryBlockSource =
        dimReadable ? mem::read<void*>(dim, offsets::Dimension_blockSource) : nullptr;
    if (verbose) {
        logger::info(
            "BLOCKSOURCE actor={} dim+0x{:X}={} dimReadable={} blockSource={} bsReadable={} valid={}",
            ptr_, offsets::Actor_dimension, dim, dimReadable, primaryBlockSource,
            mem::isReadable(primaryBlockSource, 0x100), validBlockSource(primaryBlockSource));
    }
    if (validBlockSource(primaryBlockSource)) return primaryBlockSource;

    constexpr std::ptrdiff_t candidates[] = {0x1C0, 0x1F0, 0x1D0, 0x1E0, 0x1B0, 0x200, 0x1B8,
                                             0x1C8, 0x1D8, 0x1E8, 0x1F8, 0x208, 0x210};
    for (const auto offset : candidates) {
        auto* candidateDimension = mem::read<void*>(ptr_, static_cast<std::size_t>(offset));
        const bool candidateReadable =
            candidateDimension && mem::isReadable(candidateDimension, 0x100);
        auto* blockSource =
            candidateReadable
                ? mem::read<void*>(candidateDimension, offsets::Dimension_blockSource)
                : nullptr;
        if (verbose) {
            auto** vtable = blockSource && mem::isReadable(blockSource, sizeof(void*))
                                ? *reinterpret_cast<void***>(blockSource)
                                : nullptr;
            logger::info(
                "BLOCKSOURCE candidate +0x{:X}: dim={} dimReadable={} bs={} bsReadable={} vt={} "
                "vtInModule={} vt18InModule={} valid={}",
                offset, candidateDimension, candidateReadable, blockSource,
                mem::isReadable(blockSource, 0x100), static_cast<const void*>(vtable),
                inGameModule(vtable),
                vtable ? inGameModule(vtable[offsets::BlockSource_vt_getDimensionId]) : false,
                validBlockSource(blockSource));
        }
        if (validBlockSource(blockSource)) return blockSource;
    }
    if (verbose) logger::warn("BLOCKSOURCE no valid block source for actor={}", ptr_);
    return nullptr;
}

namespace {

const std::string* hashedStringAt(const void* base, std::ptrdiff_t offset) {
    if (!base) return nullptr;
    const auto* address = static_cast<const std::uint8_t*>(base) + offset;
    if (!mem::isReadable(address, sizeof(std::string))) return nullptr;
    return reinterpret_cast<const std::string*>(address);
}

std::string safeString(const std::string* value) {
    if (!value) return {};
    const auto size = value->size();
    if (size > 256) return {};
    if (size && !mem::isReadable(value->data(), size)) return {};
    return *value;
}

}  // namespace

std::string Block::name() const {
    if (!ptr_) return {};
    const auto* blockType = mem::read<const void*>(ptr_, offsets::Block_blockType);
    if (!blockType) return {};
    const auto* value = hashedStringAt(
        blockType, offsets::BlockType_nameInfo + offsets::NameInfo_fullName +
                       offsets::HashedString_string);
    return safeString(value);
}

std::string Block::legacyName() const {
    if (!ptr_) return {};
    const auto* blockType = mem::read<const void*>(ptr_, offsets::Block_blockType);
    if (!blockType) return {};
    const auto* value =
        hashedStringAt(blockType, offsets::BlockLegacy_namespacedId + offsets::HashedString_string);
    return safeString(value);
}

}  // namespace sdk
