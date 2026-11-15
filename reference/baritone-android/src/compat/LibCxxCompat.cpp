#include <cstddef>
#include <cstdint>

namespace std {
namespace __ndk1 {

std::size_t __hash_memory(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return static_cast<std::size_t>(hash);
}

} // namespace __ndk1
} // namespace std
