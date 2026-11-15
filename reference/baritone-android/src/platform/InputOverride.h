#pragma once
#include <bitset>
#include <cstdint>
#include <optional>

#include "baritone/pathing/movement/Movement.h"
#include "sdk/Components.h"
#include "sdk/Entity.h"

namespace baritone::platform {

// Equivalent of Baritone's InputOverrideHandler: the bot "presses" keys by forcing the raw
// key state right before the game's input system reads it, then restores the real state
// so the player's own keys are not left stuck. Movement is expressed in camera-relative
// analog coordinates without writing the camera rotation.
class InputOverride {
public:
    // Apply the controls produced by the previous Baritone tick, before the game tick.
    void before(void* registry, const baritone::MovementState& controls, bool active,
                bool manipulateCamera, float turnSensitivity);
    // Restore the real raw input and write the processed state the movement system reads.
    void after();
    void reset();

private:
    void computeMovement();
    void enqueueTurn();
    void measureTurnResponse(float currentYaw);
    float currentCameraYaw() const;
    void writeRotation();

    sdk::MoveInputComponent* input_ = nullptr;
    sdk::ActorRotationComponent* rotation_ = nullptr;
    sdk::ActorHeadRotationComponent* headRotation_ = nullptr;
    sdk::MobBodyRotationComponent* bodyRotation_ = nullptr;
    void* actor_ = nullptr;
    bool manipulateCamera_ = true;
    float turnSensitivity_ = 1.0f;
    bool cameraSetterLogged_ = false;
    bool componentsLogged_ = false;
    bool applied_ = false;
    bool controlling_ = false;
    bool rotationWritten_ = false;
    bool jumpActive_ = false;
    bool prevJumpDown_ = false;
    bool prevSneakDown_ = false;
    int jumpLatch_ = 0;
    bool forced_[static_cast<std::size_t>(baritone::Input::Count)]{};
    sdk::MoveInputState savedRaw_{};

    baritone::Rotation targetRotation_{};
    bool haveTargetRotation_ = false;
    float yawResponse_ = 0.0f;
    float turnSign_ = 1.0f;
    float lastEventX_ = 0.0f;
    float lastYawBefore_ = 0.0f;
    bool lastEventValid_ = false;
    std::uint64_t turnResponses_ = 0;
    sdk::Vec2 movement_{};
    bool haveMovement_ = false;
    float cameraYaw_ = 0.0f;

    std::bitset<static_cast<std::size_t>(baritone::Input::Count)> lastInputs_{};
    bool haveLastInputs_ = false;
    std::uint64_t attempts_ = 0;
    std::uint64_t noInput_ = 0;
    std::uint64_t controlledTicks_ = 0;
    std::uint64_t movementTicks_ = 0;
    std::uint64_t turnRequests_ = 0;
};

}  // namespace baritone::platform
