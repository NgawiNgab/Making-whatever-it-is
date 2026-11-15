#pragma once

#include <camfacesdk/Fact.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camfacesdk {

// Declarative dependency: a module names the facts it needs; the registry refuses
// to run it unless every one is Available. A feature therefore cannot accidentally
// touch the game with a missing fact (docs/SPEC.md FR-3, docs/CROSS_VERSION.md R-5).
struct FactRequirement {
    // string_view, not std::string: requirement tables are declared `constexpr` and
    // therefore must not allocate. (A std::string member here fails to compile as a
    // constant expression — caught by the host syntax check, not on device.)
    std::string_view factId;
    bool required = true;  // false = nice-to-have, does not gate the module
};

// Base for every feature. Deliberately minimal: lifecycle plus a per-frame draw
// callback. Nothing in here knows about Minecraft.
class Module {
public:
    Module(std::string moduleId, std::string displayName)
        : m_id(std::move(moduleId)), m_displayName(std::move(displayName)) {}

    virtual ~Module() = default;
    Module(Module const&) = delete;
    Module& operator=(Module const&) = delete;

    [[nodiscard]] std::string const& id() const noexcept { return m_id; }
    [[nodiscard]] std::string const& displayName() const noexcept { return m_displayName; }

    // Facts this module needs before it may run.
    [[nodiscard]] virtual std::span<FactRequirement const> requirements() const {
        return {};
    }

    // Called when the user toggles the module, after its requirements are met.
    virtual void onEnable() {}
    virtual void onDisable() {}

    // Called once per frame while enabled. `draw` is the launcher's surface.
    virtual void onFrame(float /*deltaSeconds*/) {}

    // Set by the registry, never by the module.
    void setGate(bool open, std::string reason) {
        m_gateOpen = open;
        m_gateReason = std::move(reason);
    }
    [[nodiscard]] bool gateOpen() const noexcept { return m_gateOpen; }
    [[nodiscard]] std::string const& gateReason() const noexcept { return m_gateReason; }

    // Config as flat string key -> string value, mapped to the launcher's typed
    // ConfigEntry by the menu layer. Kept string-typed so no JSON dependency
    // leaks into feature code.
    virtual void onConfigChanged(std::string_view /*key*/, std::string_view /*value*/) {}

    // Asks whether the registry can satisfy every *required* fact. Implemented by
    // the caller because only it knows the resolved facts; kept pure so the rule is
    // testable without the game.
    struct GateDecision {
        bool satisfied = false;
        std::string reason = "not evaluated";
        std::vector<std::string> missing;  // required facts not available
    };

private:
    std::string m_id;
    std::string m_displayName;
    bool m_gateOpen = false;
    std::string m_gateReason = "not evaluated";
};

}  // namespace camfacesdk
