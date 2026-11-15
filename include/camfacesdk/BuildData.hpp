#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace camfacesdk {

// Everything version-specific about one game build lives in data/ and is keyed
// here. No hex literal (or any other constant) that describes the game may appear
// in src/ or include/; scripts/check_no_build_literals.sh enforces that.
//
// Adding a supported build = adding a data file + one registry entry, never a
// change to feature code.

struct SignatureEntry {
    std::string_view id;
    std::string_view pattern;       // space-separated hex, `??` wildcard (preloader syntax)
    std::size_t minSize = 0;        // structural check: not a leaflet (hook safety, HS-1)
    std::string_view expectPrefix;  // structural check: function's first bytes, hex, no spaces
};

struct RttiSlotEntry {
    std::string_view id;
    std::string_view typeInfoName;  // raw stored RTTI name, e.g. "9LevelData"
    std::size_t slot = 0;
};

// A field offset that is *only* used as an offset-delta on an already-validated
// object, never as a pointer target.
struct OffsetEntry {
    std::string_view id;
    std::string_view structName;
    std::uintptr_t delta = 0;
    std::string_view reachVia;  // the resolved accessor used instead of arithmetic, if any
};

struct BuildData {
    std::string_view mcVersion;   // e.g. "1.26.62"
    std::size_t librarySize = 0;  // identity check against the loaded image
    SignatureEntry const* signatures = nullptr;
    std::size_t signatureCount = 0;
    RttiSlotEntry const* rttiSlots = nullptr;
    std::size_t rttiSlotCount = 0;
    OffsetEntry const* offsets = nullptr;
    std::size_t offsetCount = 0;
};

// Returns the data for the given build, or nullptr when unsupported.
// nullptr is a normal, expected answer: it puts the mod in Unsupported mode where
// launcher-only features still work (docs/SPEC.md FR-5).
BuildData const* findBuild(std::string_view mcVersion);

// The build this repository has evidence for (data/1.26.62.cpp).
BuildData const& currentBuild();

}  // namespace camfacesdk
