#pragma once
#include <array>
#include <cstdint>

#include "sdk/Entity.h"
#include "sdk/Math.h"

namespace sdk {

struct StateVectorComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x1B5D5238;
    Vec3 pos;
    Vec3 posOld;
    Vec3 velocity;
};
static_assert(sizeof(StateVectorComponent) == 0x24);

struct AABBShapeComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xBAC1B3CF;
    AABB box;
    Vec2 size;  // width, height
};
static_assert(sizeof(AABBShapeComponent) == 0x20);

struct ActorRotationComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x75DF36B7;
    Vec2 rotation;  // x = pitch, y = yaw
    Vec2 rotationOld;
};
static_assert(sizeof(ActorRotationComponent) == 0x10);

struct ActorHeadRotationComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xBABE7211;
    float yHeadRot;
    float yHeadRotOld;
};
static_assert(sizeof(ActorHeadRotationComponent) == 0x8);

struct MobBodyRotationComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xD7F64BBA;
    float yBodyRot;
    float yBodyRotOld;
};
static_assert(sizeof(MobBodyRotationComponent) == 0x8);

struct ActorTypeComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x4F6BA419;
    uint32_t type;
};
static_assert(sizeof(ActorTypeComponent) == 0x4);

struct RuntimeIDComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xFC0DBBB5;
    uint64_t runtimeId;
};
static_assert(sizeof(RuntimeIDComponent) == 0x8);

// Raw key state of the movement keys, before the input system turns it into a move vector.
struct MoveInputState {
    bool sneakDown : 1;
    bool sneakToggleDown : 1;
    bool wantDownSlow : 1;
    bool wantUpSlow : 1;
    bool blockSelectDown : 1;
    bool ascendBlock : 1;
    bool descendBlock : 1;
    bool jumpDown : 1;
    bool sprintDown : 1;
    bool upLeft : 1;
    bool upRight : 1;
    bool downLeft : 1;
    bool downRight : 1;
    bool up : 1;
    bool down : 1;
    bool left : 1;
    bool right : 1;
    bool ascend : 1;
    bool descend : 1;
    bool changeHeight : 1;
    bool lookCenter : 1;
    bool sneakInputCurrentlyDown : 1;
    bool sneakInputWasReleased : 1;
    bool sneakInputWasPressed : 1;
    bool jumpInputWasReleased : 1;
    bool jumpInputWasPressed : 1;
    bool jumpInputCurrentlyDown : 1;
    Vec2 analogMoveVector;
    uint8_t lookSlightDir;
    uint8_t lookNormalDir;
    uint8_t lookSmoothDir;
};
static_assert(sizeof(MoveInputState) == 0x10);

struct MoveInputComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x018B1887;
    MoveInputState inputState;
    MoveInputState rawInputState;
    int8_t holdAutoJumpInWaterTicks;
    Vec2 move;
    Vec2 lookDelta;
    Vec2 interactDir;
    Vec3 displacement;
    Vec3 displacementDelta;
    Vec3 cameraOrientation;
    bool sneaking : 1;
    bool sprinting : 1;
    bool wantUp : 1;
    bool wantDown : 1;
    bool jumping : 1;
    bool autoJumpingInWater : 1;
    bool moveInputStateLocked : 1;
    bool persistSneak : 1;
    bool autoJumpEnabled : 1;
    bool isCameraRelativeMovementEnabled : 1;
    bool isRotControlledByMoveDirection : 1;
    std::array<bool, 2> isPaddling;
};
static_assert(sizeof(MoveInputComponent) == 0x64);

// Only the flag byte at +4 is used (bit 0 = flying).
struct MovementAbilitiesComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x6DE78DC5;
    uint8_t data[0x18];
};
static_assert(sizeof(MovementAbilitiesComponent) == 0x18);

struct ActorOwnerComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x85B93800;
    void* actor;
};
static_assert(sizeof(ActorOwnerComponent) == 0x8);

struct PlayerComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xF95D258F;
};

struct LocalPlayerComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xACB47B38;
};

// 0 survival, 1 creative, 2 adventure, 6 spectator.
struct ActorGameTypeComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x88D3EDDF;
    uint32_t gameType;
};
static_assert(sizeof(ActorGameTypeComponent) == 4);

// MinecraftCamera ECS components. Only the fields needed by the camera override are
// declared; padding mirrors the game's TypedStorage layout.
struct Quat {
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
};

struct Mat4 {
    float m[16]{};
};

struct alignas(8) CameraComponent : GameComponent {
    static constexpr uint32_t kTypeHash = componentHash("MinecraftCamera::CameraComponent");
    uint8_t id[48];
    Quat orientation;
    Vec3 position;
    float aspectRatio;
    float fieldOfView;
    float nearPlane;
    float farPlane;
    Mat4 postViewTransform;
    Mat4 savedProjection;
    Mat4 savedModelView;
};
static_assert(sizeof(CameraComponent) == 0x120);

struct CameraTargetComponent : GameComponent {
    static constexpr uint32_t kTypeHash = componentHash("MinecraftCamera::CameraTargetComponent");
    bool isOutOfRange;
    bool snappedToTarget;
    bool foundActor;
    uint8_t pad0[5];
    int64_t targetActorId;
    Vec2 currentRotation;
    Vec2 initialRotation;
    Vec2 verticalRotationLimits;
    Vec2 offset2DRotation;
    Vec3 averageRotLimitOrientation;
    Vec3 currentCameraOrientation;
    Vec3 initialCameraOrientation;
    Vec3 initialTargetCenterOffset;
    Vec3 targetCenterOffset;
    alignas(8) uint8_t targetEntity[24];
};
static_assert(sizeof(CameraTargetComponent) == 0x88);

struct TargetCameraOrientationComponent : GameComponent {
    static constexpr uint32_t kTypeHash =
        componentHash("MinecraftCamera::TargetCameraOrientationComponent");
    Quat orientation;
};
static_assert(sizeof(TargetCameraOrientationComponent) == 0x10);

struct ActiveCameraComponent : GameComponent {
    static constexpr uint32_t kTypeHash = componentHash("MinecraftCamera::ActiveCameraComponent");
};

struct CameraClientInstanceComponent : GameComponent {
    static constexpr uint32_t kTypeHash = componentHash("CameraClientInstanceComponent");
    void* clientInstance;
};
static_assert(sizeof(CameraClientInstanceComponent) == 0x8);

}  // namespace sdk
