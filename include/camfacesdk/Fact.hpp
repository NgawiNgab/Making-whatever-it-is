#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace camfacesdk {

// Resolution outcomes. Every game fact in this mod carries one of these, and a
// feature may only run when all of its facts are Available. See docs/CROSS_VERSION.md R-1..R-5.
enum class State : std::uint8_t {
    Unknown = 0,  // never attempted
    Available,    // resolved, unique, and the structural check passed
    Missing,      // pattern did not match the loaded binary
    Ambiguous,    // pattern matched more than once -> rejected, never first-matched
    Rejected,     // matched once but the landing site failed the structural check
    Unsupported,  // no build-identity match; nothing was attempted on the game
};

const char* toString(State state);

// One resolved fact: a game function or field we depend on.
//
// `address` is a live pointer at runtime (module base + rva). `rva` is stored
// because it is the only form that survives across runs (docs/CROSS_VERSION.md
// section 6.1); live addresses are never written to disk.
struct Fact {
    std::string id;
    State state = State::Unknown;
    std::uintptr_t address = 0;
    std::uintptr_t rva = 0;
    std::uint32_t matches = 0;

    // Measured lower bound on the function's size, carried from the build data so
    // the hook layer can enforce HS-1 (never patch wider than the function)
    // without re-deriving it from a string. 0 = not measured.
    std::size_t minSize = 0;

    // The bytes the site is expected to start with (from the structural check that
    // produced State::Available). The hook re-compares these after patching, so a
    // "verified" hook that landed somewhere unexpected is caught instead of trusted.
    std::array<std::uint8_t, 4> expectPrefix{};
    std::uint32_t expectPrefixLength = 0;

    std::string detail;  // human-readable reason / check result

    [[nodiscard]] bool available() const noexcept { return state == State::Available; }
};

}  // namespace camfacesdk
