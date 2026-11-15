#pragma once

#include <camfacesdk/BuildData.hpp>
#include <camfacesdk/Fact.hpp>
#include <camfacesdk/Module.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camfacesdk {

// Provides the module base address and image bytes for the loaded game library.
// Separated behind an interface so resolution can be unit-tested on the host with
// a synthetic buffer (no Android, no game binary).
class ImageSource {
public:
    virtual ~ImageSource() = default;
    [[nodiscard]] virtual std::uintptr_t base() const = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
    // Reads the image, copying at most `length` bytes into `out`.
    virtual bool read(std::size_t offset, std::span<std::uint8_t> out) = 0;
    [[nodiscard]] virtual bool get() { return true; }
};

// Resolves every fact the given build declares, against the currently loaded game
// image, and records the outcome. This is the only place in the mod that talks to
// the game's code addresses.
class FactRegistry {
public:
    // `image` supplies the loaded game library. Passing nullptr (or an image that
    // cannot be read) leaves every fact Missing, which is the correct behaviour on
    // an unsupported/not-yet-mapped game. Resolution happens once; call reset()
    // to redo it.
    bool resolveAll(ImageSource* image, BuildData const& build);

    void reset();

    [[nodiscard]] Fact const* find(std::string_view id) const;
    [[nodiscard]] std::vector<Fact> const& all() const noexcept { return m_facts; }

    // True only if every listed requirement with `required` set is AVAILABLE.
    // Modules call this before touching the game; a single missing required fact
    // keeps the module closed (docs/SPEC.md FR-3).
    [[nodiscard]] bool satisfies(std::span<FactRequirement const> requirements,
                                 std::vector<std::string>* missing) const;
    [[nodiscard]] bool resolved() const noexcept { return m_resolved; }

    // Writes a machine-readable report next to the engine's other outputs. Returns
    // the report text as well so it can be logged. Never contains live addresses:
    // only RVAs survive a run (docs/CROSS_VERSION.md section 6.1).
    std::string writeReport(std::string const& path) const;

    // Summary for one line of log output, e.g. "3 available, 1 missing".
    [[nodiscard]] std::string summary() const;

private:
    Fact* findMutable(std::string_view id);

    std::vector<Fact> m_facts;
    bool m_resolved = false;
};

// Resolves facts against an arbitrary ImageSource. FactRegistry::resolveAll uses
// this with the real game image; tests use it with a fixture. Exposed so the
// resolution rules have exactly one implementation.
struct ResolveOptions {
    std::size_t matchLimit = 8;
    bool runStructuralChecks = true;
};

void resolveAgainst(ImageSource& image, BuildData const& build, std::vector<Fact>& out,
                    ResolveOptions const& options = {});

}  // namespace camfacesdk
