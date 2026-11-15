// Host-build stubs for the preloader symbols the mod layer references.
//
// These do nothing. They exist so the mod layer can be linked and unit tested on a
// desktop; in a real build the preloader provides the real ones at runtime. Never
// ship this file.
#include <cstdint>
#include <span>
#include <string_view>

#include "pl/ModMenu.hpp"
#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"

namespace pl::memory {
int hook(FuncPtr, FuncPtr, FuncPtr*, HookPriority) { return -1; }
bool unhook(FuncPtr, FuncPtr) { return false; }
uintptr_t resolveSignature(std::string_view, std::string_view) { return 0; }
}  // namespace pl::memory

namespace pl::modmenu {
void submitDrawCommands(std::string_view, std::span<const DrawCommand>) {}
HudSurfaceSize getHudSurfaceSize() { return {}; }
}  // namespace pl::modmenu

// hooks::setPendingTurn is defined in hooks/GameHooks.cpp, which cannot be linked on
// a desktop (it hooks live game memory). InputOverride calls it to turn the camera.
namespace baritone::hooks {
void setPendingTurn(float, float, int, int) {}
}  // namespace baritone::hooks
