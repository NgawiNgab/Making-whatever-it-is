#include <camfacesdk/Pattern.hpp>

#include <cctype>
#include <string_view>

namespace camfacesdk {
namespace {

int hexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// `??` and `?` are full wildcards. `?4`, `4?`, `A?` are masked bytes. Anything
// else is malformed. Mirrors preloader's accepted grammar (Signature.cpp:64-96).
bool parseToken(std::string_view token, PatternToken& out) {
    if (token == "?" || token == "??") {
        out = PatternToken{0, 0};
        return true;
    }
    if (token.size() != 2) return false;

    std::uint8_t value = 0;
    std::uint8_t mask = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        const char ch = token[i];
        const auto shift = static_cast<std::uint8_t>((1 - i) * 4);
        if (ch == '?') continue;
        const int digit = hexDigit(ch);
        if (digit < 0) return false;
        value = static_cast<std::uint8_t>(value | (digit << shift));
        mask = static_cast<std::uint8_t>(mask | (0xF << shift));
    }
    out = PatternToken{value, mask};
    return true;
}

bool isSpace(char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; }

}  // namespace

namespace {

// Appends one whitespace-delimited token. A token is either a single byte
// (`AA`, `??`, `?4`) or a run of unspaced hex bytes (`fd7bbda9`), which the
// engine's parser also accepts — patterns copied out of tooling arrive in both
// forms, so accepting only one of them silently rejects valid signatures.
bool appendToken(std::string_view token, ParsedPattern& out) {
    PatternToken parsed;
    if (parseToken(token, parsed)) {
        if (out.size >= ParsedPattern::kMaxBytes) return false;
        out.tokens[out.size++] = parsed;
        return true;
    }

    if (token.empty() || token.size() % 2 != 0) return false;
    for (std::size_t pos = 0; pos < token.size(); pos += 2) {
        if (!parseToken(token.substr(pos, 2), parsed)) return false;
        if (out.size >= ParsedPattern::kMaxBytes) return false;
        out.tokens[out.size++] = parsed;
    }
    return true;
}

}  // namespace

bool parsePattern(std::string_view text, ParsedPattern& out) {
    out = ParsedPattern{};
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && isSpace(text[pos])) ++pos;
        if (pos >= text.size()) break;

        const std::size_t start = pos;
        while (pos < text.size() && !isSpace(text[pos])) ++pos;
        if (!appendToken(text.substr(start, pos - start), out)) {
            out = ParsedPattern{};
            return false;
        }
    }
    return out.size != 0;
}

bool matchesAt(std::span<const std::uint8_t> data, std::size_t offset,
               ParsedPattern const& pattern) {
    if (pattern.empty()) return false;
    if (offset > data.size() || data.size() - offset < pattern.size) return false;

    for (std::size_t i = 0; i < pattern.size; ++i) {
        const PatternToken token = pattern.tokens[i];
        if (token.isWildcard()) continue;
        if ((data[offset + i] & token.mask) != token.value) return false;
    }
    return true;
}

std::size_t countMatches(std::span<const std::uint8_t> data, ParsedPattern const& pattern,
                         std::size_t limit) {
    if (pattern.empty() || pattern.size > data.size() || limit == 0) return 0;

    // Anchor on the longest run of fully-specified bytes, the same optimisation
    // preloader uses (Signature.cpp:147-190), so scanning a 340 MB image is
    // practical. Correctness never depends on the anchor: every candidate is
    // verified against the whole pattern by matchesAt.
    std::size_t anchorIndex = 0;
    std::size_t anchorSize = 0;
    for (std::size_t runStart = 0; runStart < pattern.size;) {
        if (pattern.tokens[runStart].isWildcard() || pattern.tokens[runStart].mask != 0xFF) {
            ++runStart;
            continue;
        }
        std::size_t runEnd = runStart;
        while (runEnd < pattern.size && pattern.tokens[runEnd].mask == 0xFF) ++runEnd;
        const std::size_t runLength = runEnd - runStart;
        if (runLength > anchorSize) {
            anchorSize = runLength;
            anchorIndex = runEnd - runLength;
        }
        runStart = runEnd;
    }

    std::size_t found = 0;
    const PatternToken anchor = pattern.tokens[anchorIndex];
    const std::size_t lastStart = data.size() - pattern.size;

    for (std::size_t candidate = 0; candidate <= lastStart; ++candidate) {
        if (anchorSize != 0) {
            // Fast reject on the anchor's first byte before the full compare.
            if (data[candidate + anchorIndex] != anchor.value) continue;
        }
        if (!matchesAt(data, candidate, pattern)) continue;

        ++found;
        if (found >= limit) return found;
        // Overlapping matches are counted separately; a pattern that matches
        // inside itself is still ambiguous, which is what we want to report.
    }
    return found;
}

}  // namespace camfacesdk
