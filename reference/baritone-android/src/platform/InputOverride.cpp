#include "platform/InputOverride.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "baritone/utils/Rotation.h"
#include "hooks/GameHooks.h"
#include "platform/Log.h"
#include "sdk/Game.h"

namespace baritone::platform {
namespace {

std::size_t index(baritone::Input input) {
    return static_cast<std::size_t>(input);
}



std::string inputNames(const std::bitset<static_cast<std::size_t>(baritone::Input::Count)>& inputs) {
    static constexpr const char* names[] = {"forward", "back", "left", "right", "clickL",
                                            "clickR",  "jump", "sneak", "sprint"};
    std::string out;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (!inputs.test(i)) continue;
        if (!out.empty()) out += ',';
        out += names[i];
    }
    return out.empty() ? "none" : out;
}

std::string rawText(const sdk::MoveInputState& s) {
    return std::format(
        "up={} down={} left={} right={} jmp={} snk={} spr={} analog=({:.2f},{:.2f})",
        static_cast<bool>(s.up), static_cast<bool>(s.down), static_cast<bool>(s.left),
        static_cast<bool>(s.right), static_cast<bool>(s.jumpDown),
        static_cast<bool>(s.sneakDown), static_cast<bool>(s.sprintDown), s.analogMoveVector.x,
        s.analogMoveVector.y);
}

}  // namespace

void InputOverride::reset() {
    input_ = nullptr;
    rotation_ = nullptr;
    headRotation_ = nullptr;
    bodyRotation_ = nullptr;
    actor_ = nullptr;
    applied_ = false;
    controlling_ = false;
    rotationWritten_ = false;
    jumpActive_ = false;
    haveTargetRotation_ = false;
    haveMovement_ = false;
    movement_ = {};
    cameraYaw_ = 0.0f;
    std::fill(std::begin(forced_), std::end(forced_), false);
}

float InputOverride::currentCameraYaw() const {
    if (input_) {
        const float fx = input_->cameraOrientation.x;
        const float fz = input_->cameraOrientation.z;
        if (fx * fx + fz * fz > 1.0e-6f) {
            return static_cast<float>(std::atan2(-fx, fz) * 57.29577951308232);
        }
    }
    if (rotation_) return rotation_->rotation.y;
    return 0.0f;
}

void InputOverride::computeMovement() {
    haveMovement_ = false;
    movement_ = {};
    if (!input_) return;

    const bool fwd = forced_[index(baritone::Input::MoveForward)];
    const bool back = forced_[index(baritone::Input::MoveBack)];
    const bool left = forced_[index(baritone::Input::MoveLeft)];
    const bool right = forced_[index(baritone::Input::MoveRight)];
    if (!(fwd || back || left || right)) return;

    float x = static_cast<float>(left) - static_cast<float>(right);
    float y = static_cast<float>(fwd) - static_cast<float>(back);
    const float len = std::sqrt(x * x + y * y);
    if (len > 1.0f) {
        x /= len;
        y /= len;
    }

    // Baritone's key inputs are relative to its target yaw. The game applies them
    // relative to the actual camera yaw, so rotate the vector by the difference.
    // Movement then keeps its intended world direction while the camera turns
    // independently and can stop once it is aimed.
    const float cameraYaw = currentCameraYaw();
    cameraYaw_ = cameraYaw;

    if (haveTargetRotation_) {
        const float delta =
            baritone::Rotation::normalizeYaw(targetRotation_.yaw - cameraYaw) * 0.017453292519943295f;
        const float c = std::cos(delta);
        const float s = std::sin(delta);
        const float rx = x * c - y * s;
        const float ry = x * s + y * c;
        x = rx;
        y = ry;
    }

    movement_.x = x;
    movement_.y = y;
    haveMovement_ = x != 0.0f || y != 0.0f;
}

void InputOverride::measureTurnResponse(float currentYaw) {
    if (!lastEventValid_) return;
    const float delta = currentYaw - lastYawBefore_;
    if (std::fabs(lastEventX_) > 1.0e-6f) {
        const float observed = delta / lastEventX_;
        if (std::isfinite(observed) && std::fabs(observed) > 0.5f &&
            std::fabs(observed) < 100000.0f) {
            yawResponse_ = observed;
            if (turnResponses_++ < 20) {
                logger::warn(
                    "INPUT turn response event={:.4f} delta={:.2f} observed={:.2f} sign={:.0f}",
                    lastEventX_, delta, observed, turnSign_);
            }
        }
    }
    lastEventValid_ = false;
}

void InputOverride::enqueueTurn() {
    if (!haveTargetRotation_ || !manipulateCamera_) return;

    const float currentYaw = currentCameraYaw();
    const float currentPitch = rotation_ ? rotation_->rotation.x : 0.0f;
    const float rawTargetYaw = targetRotation_.yaw;
    const float dyaw = std::remainder(rawTargetYaw - currentYaw, 360.0f);
    const float dpitch =
        std::clamp(targetRotation_.pitch, -90.0f, 90.0f) - currentPitch;
    if (std::fabs(dyaw) < 15.0f && std::fabs(dpitch) < 15.0f) return;

    constexpr float kGain = 0.5f;
    constexpr float kMaxEvent = 0.1f;
    constexpr float kYawResponse = 300.0f;
    const float damping = std::clamp(turnSensitivity_, 0.1f, 10.0f);
    const float x = std::clamp(turnSign_ * kGain * dyaw / (kYawResponse * damping),
                               -kMaxEvent, kMaxEvent);
    const float y = 0.0f;
    lastEventX_ = x;
    lastYawBefore_ = currentYaw;
    lastEventValid_ = true;
    hooks::setPendingTurn(x, y, 2, -1);

    const auto request = turnRequests_++;
    if (request < 3) {
        logger::warn("INPUT turn request target={:.2f} current={:.2f} dyaw={:.2f} "
                     "dpitch={:.2f} sens={:.3f} resp={:.2f} sign={:.0f} event=({:.4f},{:.4f})",
                     rawTargetYaw, currentYaw, dyaw, dpitch, turnSensitivity_,
                     yawResponse_, turnSign_, x, y);
    } else if (request % 20 == 0) {
        logger::info("INPUT turn request target={:.2f} current={:.2f} dyaw={:.2f} "
                     "dpitch={:.2f} sens={:.3f} resp={:.2f} sign={:.0f} event=({:.4f},{:.4f})",
                     rawTargetYaw, currentYaw, dyaw, dpitch, turnSensitivity_,
                     yawResponse_, turnSign_, x, y);
    }
}

void InputOverride::writeRotation() {
    if (!haveTargetRotation_ || !manipulateCamera_ || !input_) return;

    const float currentYaw = currentCameraYaw();
    const float dyaw = std::remainder(targetRotation_.yaw - currentYaw, 360.0f);
    if (std::fabs(dyaw) < 15.0f) {
        input_->lookDelta = {};
        return;
    }

    const float absDyaw = std::fabs(dyaw);
    const float speed = std::clamp(absDyaw / 60.0f, 0.15f, 1.0f);
    const float maxStep = 30.0f / std::clamp(turnSensitivity_, 0.1f, 10.0f);
    const float step = std::copysign(std::min(absDyaw, maxStep * speed), dyaw);
    const sdk::Vec2 oldLook = input_->lookDelta;
    input_->lookDelta.x = 0.0f;
    input_->lookDelta.y = turnSign_ * step;

    if (turnRequests_++ < 5) {
        logger::warn("INPUT lookDelta turn current={:.2f} target={:.2f} dyaw={:.2f} "
                     "step={:.2f} sens={:.2f} sign={:.0f} old=({:.4f},{:.4f}) new=({:.4f},{:.4f})",
                     currentYaw, targetRotation_.yaw, dyaw, step, turnSensitivity_, turnSign_,
                     oldLook.x, oldLook.y, input_->lookDelta.x, input_->lookDelta.y);
    }
}

void InputOverride::before(void* registry, const baritone::MovementState& controls, bool active,
                           bool manipulateCamera, float turnSensitivity) {
    reset();
    manipulateCamera_ = manipulateCamera;
    turnSensitivity_ = std::clamp(turnSensitivity, 0.1f, 10.0f);
    ++attempts_;
    if (!registry) return;

    sdk::GameRegistry reg(registry);
    if (!reg.valid()) return;
    auto entity = reg.playerEntity();
    if (!entity) return;
    auto* input = reg.tryGet<sdk::MoveInputComponent>(*entity);
    if (!input) {
        if (++noInput_ <= 3 || noInput_ % 20 == 0)
            logger::warn("INPUT no MoveInputComponent for entity=0x{:08X}", entity->raw);
        return;
    }

    input_ = input;
    rotation_ = reg.tryGet<sdk::ActorRotationComponent>(*entity);
    headRotation_ = reg.tryGet<sdk::ActorHeadRotationComponent>(*entity);
    bodyRotation_ = reg.tryGet<sdk::MobBodyRotationComponent>(*entity);
    actor_ = reg.actorFor(*entity);
    if (rotation_) {
        measureTurnResponse(currentCameraYaw());
    } else {
        lastEventValid_ = false;
    }
    if (!componentsLogged_) {
        logger::info("CAMERA components actor={} rotation={} head={} body={}", actor_,
                     static_cast<void*>(rotation_), static_cast<void*>(headRotation_),
                     static_cast<void*>(bodyRotation_));
        componentsLogged_ = true;
    }

    if (active && controls.target.rotation) {
        targetRotation_ = *controls.target.rotation;
        haveTargetRotation_ = true;
    }
    if (active && !rotation_ && attempts_ % 100 == 0) {
        logger::warn("MOVE no ActorRotationComponent for entity=0x{:08X}", entity->raw);
    }

    forced_[index(baritone::Input::MoveForward)] = controls.input(baritone::Input::MoveForward);
    forced_[index(baritone::Input::MoveBack)] = controls.input(baritone::Input::MoveBack);
    forced_[index(baritone::Input::MoveLeft)] = controls.input(baritone::Input::MoveLeft);
    forced_[index(baritone::Input::MoveRight)] = controls.input(baritone::Input::MoveRight);
    forced_[index(baritone::Input::Jump)] = controls.input(baritone::Input::Jump);
    forced_[index(baritone::Input::Sneak)] = controls.input(baritone::Input::Sneak);
    forced_[index(baritone::Input::Sprint)] = controls.input(baritone::Input::Sprint);

    const bool hasMovement = controls.input(baritone::Input::MoveForward) ||
                             controls.input(baritone::Input::MoveBack) ||
                             controls.input(baritone::Input::MoveLeft) ||
                             controls.input(baritone::Input::MoveRight) ||
                             controls.input(baritone::Input::Jump) ||
                             controls.input(baritone::Input::Sneak) ||
                             controls.input(baritone::Input::Sprint);
    controlling_ = hasMovement;
    if (!controlling_) {
        jumpLatch_ = 0;
        jumpActive_ = false;
        prevJumpDown_ = false;
        prevSneakDown_ = false;
        if (haveLastInputs_) {
            logger::info("INPUT release controls (mask={})", controls.inputs.to_string());
            haveLastInputs_ = false;
        }
        return;
    }

    // Controls are applied one tick after they are produced and Bedrock's jump is edge
    // sensitive, so hold a requested jump for a second tick.
    const bool jumpRequested = forced_[index(baritone::Input::Jump)];
    if (jumpRequested) jumpLatch_ = 2;
    jumpActive_ = jumpRequested || jumpLatch_ > 0;
    if (jumpLatch_ > 0) --jumpLatch_;

    computeMovement();

    savedRaw_ = input->rawInputState;
    const bool changed = !haveLastInputs_ || controls.inputs != lastInputs_;
    sdk::MoveInputState& raw = input->rawInputState;
    raw.up = raw.down = raw.left = raw.right = false;
    raw.upLeft = raw.upRight = raw.downLeft = raw.downRight = false;
    raw.jumpDown = raw.sneakDown = raw.sprintDown = false;
    raw.analogMoveVector = {};

    raw.up = movement_.y > 0.1f;
    raw.down = movement_.y < -0.1f;
    raw.left = movement_.x > 0.1f;
    raw.right = movement_.x < -0.1f;
    raw.jumpDown = jumpActive_;
    raw.jumpInputCurrentlyDown = jumpActive_;
    raw.jumpInputWasPressed = jumpActive_ && !prevJumpDown_;
    raw.jumpInputWasReleased = !jumpActive_ && prevJumpDown_;
    prevJumpDown_ = jumpActive_;
    raw.sneakDown = forced_[index(baritone::Input::Sneak)];
    raw.sneakInputCurrentlyDown = raw.sneakDown;
    raw.sneakInputWasPressed = raw.sneakDown && !prevSneakDown_;
    raw.sneakInputWasReleased = !raw.sneakDown && prevSneakDown_;
    prevSneakDown_ = raw.sneakDown;
    raw.sprintDown = forced_[index(baritone::Input::Sprint)];
    if (haveMovement_) raw.analogMoveVector = movement_;
    applied_ = true;

    ++movementTicks_;
    if (changed || controlledTicks_ < 2 || movementTicks_ % 20 == 0) {
        logger::info("INPUT before inputs='{}' mask={} move=({:.2f},{:.2f}) cameraYaw={:.2f} "
                     "targetYaw={:.2f} actualYaw={:.2f} actualPitch={:.2f} wroteRot={} jump={} {}",
                     inputNames(controls.inputs), controls.inputs.to_string(), movement_.x,
                     movement_.y, cameraYaw_,
                     haveTargetRotation_ ? targetRotation_.yaw : 0.0f,
                     rotation_ ? rotation_->rotation.y : 0.0f,
                     rotation_ ? rotation_->rotation.x : 0.0f, rotationWritten_, jumpActive_,
                     rawText(raw));
        lastInputs_ = controls.inputs;
        haveLastInputs_ = true;
    }
}

void InputOverride::after() {
    if (!applied_ || !input_) {
        input_ = nullptr;
        return;
    }

    sdk::MoveInputComponent& input = *input_;
    input.rawInputState = savedRaw_;

    // Also write the processed state the movement system reads. When the window has no
    // focus the game ignores the raw key state, so this is what makes the bot move then.
    sdk::MoveInputState& st = input.inputState;
    const bool jump = jumpActive_;
    const bool sneak = forced_[index(baritone::Input::Sneak)];
    const bool sprint = forced_[index(baritone::Input::Sprint)];

    if (controlling_) {
        st.up = movement_.y > 0.1f;
        st.down = movement_.y < -0.1f;
        st.left = movement_.x > 0.1f;
        st.right = movement_.x < -0.1f;
        st.jumpDown = st.sneakDown = st.sprintDown = false;
    }
    if (jump) {
        st.jumpDown = true;
        st.jumpInputCurrentlyDown = true;
    }
    if (sneak) st.sneakDown = true;
    if (sprint) st.sprintDown = true;
    if (controlling_) {
        input.move = movement_;
        st.analogMoveVector = movement_;
    }
    if (controlling_ || jump) input.jumping = jump;
    if (controlling_ || sneak) input.sneaking = sneak;
    if (controlling_ || sprint) input.sprinting = sprint;

    writeRotation();

    ++controlledTicks_;
    if (controlledTicks_ <= 2 || controlledTicks_ % 100 == 0) {
        logger::info(
            "INPUT after tick={} applied={} move=({:.2f},{:.2f}) cameraYaw={:.2f} "
            "targetYaw={:.2f} input[{}] jumping={} sneaking={} sprinting={}",
            controlledTicks_, applied_, input.move.x, input.move.y, cameraYaw_,
            haveTargetRotation_ ? targetRotation_.yaw : 0.0f, rawText(st),
            static_cast<bool>(input.jumping), static_cast<bool>(input.sneaking),
            static_cast<bool>(input.sprinting));
    }

    input_ = nullptr;
    applied_ = false;
}

}  // namespace baritone::platform
