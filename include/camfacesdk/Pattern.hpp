#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace camfacesdk {

struct PatternToken {
    std::uint8_t value = 0;
    std::uint8_t mask = 0;
    constexpr bool isWildcard() const noexcept { return mask == 0; }
};

struct ParsedPattern {
    // Fixed-capacity so this stays usable in freestanding/host-test contexts with
    // no heap. A pattern longer than this is rejected rather than truncated.
    static constexpr std::size_t kMaxBytes = 256;
    PatternToken tokens[kMaxBytes]{};
    std::size_t size = 0;

    [[nodiscard]] bool empty() const noexcept { return size == 0; }
};

// Parses the signature syntax used by preloader and by Zaphkiel's generator:
// space-separated hex byte tokens with `?`/`??` wildcards and partial-byte masks
// such as `?4` / `A?`. Returns false on malformed input, odd-length tokens, or
// overflow — never silently truncates.
bool parsePattern(std::string_view text, ParsedPattern& out);

// Byte-level matcher over a caller-supplied region. Split out from scanning so it
// is unit-testable on the host with synthetic buffers.
[[nodiscard]] bool matchesAt(std::span<const std::uint8_t> data, std::size_t offset,
                             ParsedPattern const& pattern);

// Counts every occurrence of the pattern in the region. Counting (rather than
// stopping at the first hit) is the whole point: a pattern with two matches is
// not an identity, and treating it as one is the reference's core hazard
// (docs/CROSS_VERSION.md R-1, evidence F-CAM-15).
//
// `firstOffset` receives the offset of the first match (or npos). Stops early
// once `limit` matches are found, which is enough to classify as ambiguous.
std::size_t countMatches(std::span<const std::uint8_t> data, ParsedPattern const& pattern,
                         std::size_t limit = 8);

}  // namespace camfacesdk
