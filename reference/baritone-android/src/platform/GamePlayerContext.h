#pragma once
#include <optional>

#include "baritone/api/PlayerContext.h"
#include "sdk/Entity.h"

namespace sdk {
struct AABBShapeComponent;
struct ActorRotationComponent;
struct MoveInputComponent;
struct MovementAbilitiesComponent;
struct StateVectorComponent;
}  // namespace sdk

namespace baritone::platform {

// Minimal PlayerContext over the live game objects. Bound to the local player each tick.
// Inventory, containers, entity interaction and block actions are not implemented yet:
// the pathfinder only needs the player state and the world snapshot.
class GamePlayerContext final : public baritone::PlayerContext {
public:
    void bind(void* registry, void* actor, void* blockSource, std::optional<EntityId> entity = std::nullopt);
    void unbind();

    bool valid() const override;
    baritone::Vec3d position() const override;
    baritone::Vec3d eyePosition() const override;
    baritone::Vec3d velocity() const override;
    baritone::Rotation rotation() const override;
    baritone::Vec3d cameraDirection() const;
    bool onGround() const override;
    bool isSneaking() const override;
    bool isInWater() const override;
    double blockReachDistance() const override;

    int selectedSlot() const override;
    void selectSlot(int slot) override;
    baritone::HotbarStack hotbar(int slot) const override;
    int countItem(std::string_view name) const override;

    std::vector<baritone::EntityInfo> entities() const override;

    bool startOrContinueBreaking(const baritone::BlockPos& pos, baritone::Direction face) override;
    void stopBreaking() override;
    void useItemOn(const baritone::BlockPos& pos, baritone::Direction face, const baritone::Vec3d& hit) override;
    void swingArm() override;

    void refreshWorld(const baritone::BlockPos& center, int radius) override;

    bool isFlying() const override;
    bool isCreative() const override;
    int foodLevel() const override;
    float health() const override;
    int effectAmplifier(std::string_view effect) const override;

    void* actor() const { return actor_; }
    void* blockSource() const { return region_; }

private:
    const sdk::StateVectorComponent* state() const;
    const sdk::AABBShapeComponent* aabb() const;
    const sdk::ActorRotationComponent* actorRotation() const;
    const sdk::MoveInputComponent* moveInput() const;
    const sdk::MovementAbilitiesComponent* abilities() const;

    void* registry_ = nullptr;
    void* actor_ = nullptr;
    void* region_ = nullptr;
    EntityId entity_{};
    bool haveEntity_ = false;
};

}  // namespace baritone::platform
