#pragma once
#include <cstdint>

namespace baritone::hooks {

bool install();
void uninstall();

// Registry captured from the last ClientInputUpdateSystemInternal tick.
void* registry();
std::uint64_t tickCount();

// InputHandler/IClientInstance captured from the last InputHandler::tick.
void* inputHandler();
void* clientInstance();

// Queue a synthetic touch Turn event for the next InputHandler::tick.
void setPendingTurn(float x, float y, int focusImpact, int controllerId);

void logDiagnostics(void* registry);

}  // namespace baritone::hooks
