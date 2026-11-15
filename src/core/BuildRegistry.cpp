#include <camfacesdk/BuildData.hpp>

#include <array>

namespace camfacesdk::data {
BuildData const& build_1_26_62();
}

namespace camfacesdk {
namespace {

// Built builds are appended here. One line per supported game build; the build's
// own values live in its data/*.cpp, never in this list.
using Lookup = BuildData const& (*)();
constexpr std::array<Lookup, 1> kSupported{&data::build_1_26_62};

}  // namespace

BuildData const* findBuild(std::string_view mcVersion) {
    for (Lookup lookup : kSupported) {
        BuildData const& candidate = lookup();
        if (candidate.mcVersion == mcVersion) return &candidate;
    }
    return nullptr;
}

BuildData const& currentBuild() { return data::build_1_26_62(); }

}  // namespace camfacesdk
