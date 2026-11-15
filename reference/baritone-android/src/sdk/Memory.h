#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace mem {

struct Range {
    std::uintptr_t start;
    std::uintptr_t end;
};

// Android ARM64 uses the top byte for MTE/TBI tags. The kernel mappings in
// /proc/self/maps are untagged, so strip the tag before range comparisons.
inline std::uintptr_t addressOf(const void* ptr) {
    return reinterpret_cast<std::uintptr_t>(ptr) & 0x00FFFFFFFFFFFFFFull;
}

template <typename T>
T read(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
T* member(void* base, std::size_t offset) {
    return reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

template <typename T>
const T* member(const void* base, std::size_t offset) {
    return reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(base) + offset);
}

template <typename Fn>
Fn function(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

// True when [ptr, ptr+size) is inside a readable mapping of this process.
bool isReadable(const void* ptr, std::size_t size);

// Overall address range of the readable mappings belonging to a loaded module.
bool moduleRange(std::string_view moduleName, Range& out);

}  // namespace mem
