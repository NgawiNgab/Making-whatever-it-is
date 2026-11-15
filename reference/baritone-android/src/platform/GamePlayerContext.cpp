#include "platform/GamePlayerContext.h"

#include <cmath>

#include "baritone/Settings.h"
#include "baritone/utils/Rotation.h"
#include "baritone/world/BlockStateInterface.h"
#include "platform/WorldTracker.h"
#include "sdk/Components.h"
#include "sdk/Game.h"
#include "sdk/Memory.h"
#include "sdk/Offsets.h"

namespace baritone::platform {

using baritone::BlockPos;
using baritone::Vec3d;

namespace {

sdk::GameRegistry registryOf(void* registry) {
    return sdk::GameRegistry(registry);
}

Vec3d toVec(const sdk::Vec3& v) {
    return {v.x, v.y, v.z};
}

}  // namespace

void GamePlayerContext::bind(void* registry, void* actor, void* blockSource, std::optional<EntityId> entity) {
    registry_ = registry;
    actor_ = actor;
    region_ = blockSource;
    haveEntity_ = false;
    entity_ = {};
    if (entity) {
        entity_ = *entity;
        haveEntity_ = true;
        return;
    }
    if (!actor) return;

    sdk::Actor view(actor);
    if (auto* context = view.entityContext(); context && mem::isReadable(context, sizeof(*context))) {
        entity_ = context->entity;
        haveEntity_ = true;
    }
}

void GamePlayerContext::unbind() {
    registry_ = nullptr;
    actor_ = nullptr;
    region_ = nullptr;
    entity_ = {};
    haveEntity_ = false;
}

bool GamePlayerContext::valid() const {
    return registry_ && actor_ && region_ && haveEntity_;
}

const sdk::StateVectorComponent* GamePlayerContext::state() const {
    if (!registry_ || !haveEntity_) return nullptr;
    return registryOf(registry_).tryGet<sdk::StateVectorComponent>(entity_);
}

const sdk::AABBShapeComponent* GamePlayerContext::aabb() const {
    if (!registry_ || !haveEntity_) return nullptr;
    return registryOf(registry_).tryGet<sdk::AABBShapeComponent>(entity_);
}

const sdk::ActorRotationComponent* GamePlayerContext::actorRotation() const {
    if (!registry_ || !haveEntity_) return nullptr;
    return registryOf(registry_).tryGet<sdk::ActorRotationComponent>(entity_);
}

const sdk::MoveInputComponent* GamePlayerContext::moveInput() const {
    if (!registry_ || !haveEntity_) return nullptr;
    return registryOf(registry_).tryGet<sdk::MoveInputComponent>(entity_);
}

const sdk::MovementAbilitiesComponent* GamePlayerContext::abilities() const {
    if (!registry_ || !haveEntity_) return nullptr;
    return registryOf(registry_).tryGet<sdk::MovementAbilitiesComponent>(entity_);
}

Vec3d GamePlayerContext::position() const {
    // The AABB is authoritative for the feet; the state vector of players is at eye height.
    if (const auto* box = aabb()) {
        return {(box->box.min.x + box->box.max.x) * 0.5, box->box.min.y,
                (box->box.min.z + box->box.max.z) * 0.5};
    }
    if (const auto* st = state()) return {st->pos.x, st->pos.y - 1.62, st->pos.z};
    return {};
}

Vec3d GamePlayerContext::eyePosition() const {
    if (const auto* st = state()) return toVec(st->pos);
    Vec3d p = position();
    p.y += 1.62;
    return p;
}

Vec3d GamePlayerContext::velocity() const {
    if (const auto* st = state()) return toVec(st->velocity);
    return {};
}

baritone::Rotation GamePlayerContext::rotation() const {
    if (const auto* r = actorRotation()) return {r->rotation.y, r->rotation.x};
    return {};
}

baritone::Vec3d GamePlayerContext::cameraDirection() const {
    if (const auto* input = moveInput(); input && input->isCameraRelativeMovementEnabled) {
        const baritone::Vec3d value{input->cameraOrientation.x, input->cameraOrientation.y,
                                    input->cameraOrientation.z};
        if (std::isfinite(value.lengthSq()) && value.lengthSq() > 1.0e-6) {
            const double length = std::sqrt(value.lengthSq());
            return value * (1.0 / length);
        }
    }
    return rotation_utils::calcLookDirection(rotation());
}

bool GamePlayerContext::onGround() const {
    // Standing on something: the top of a colliding block under one corner of the
    // bounding box is at feet level and we're not going up.
    if (velocity().y > 0.01) return false;
    auto snapshot = WorldTracker::get().snapshot();
    if (!snapshot) return false;
    const auto* box = aabb();
    if (!box) return false;

    baritone::BlockStateInterface bsi(snapshot);
    const double feetY = box->box.min.y;
    const int by = static_cast<int>(std::floor(feetY - 0.01));
    const double xs[2] = {box->box.min.x + 0.001, box->box.max.x - 0.001};
    const double zs[2] = {box->box.min.z + 0.001, box->box.max.z - 0.001};
    for (double x : xs) {
        for (double z : zs) {
            const int bx = static_cast<int>(std::floor(x)), bz = static_cast<int>(std::floor(z));
            for (int y : {by, by - 1}) {  // fences are 1.5 high
                const baritone::BlockProps& p = bsi.props(bx, y, bz);
                if (!p.collides) continue;
                const double top = y + (p.topSlab ? 1.0 : static_cast<double>(p.height));
                if (std::fabs(feetY - top) < 0.02) return true;
            }
        }
    }
    return false;
}

bool GamePlayerContext::isSneaking() const {
    const auto* input = moveInput();
    return input && input->sneaking;
}

bool GamePlayerContext::isInWater() const {
    auto snapshot = WorldTracker::get().snapshot();
    if (!snapshot) return false;
    baritone::BlockStateInterface bsi(snapshot);
    const Vec3d p = position();
    return bsi
               .props(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)),
                      static_cast<int>(std::floor(p.z)))
               .fluid == baritone::Fluid::Water;
}

double GamePlayerContext::blockReachDistance() const {
    return baritone::Settings::get().blockReachDistance;
}

int GamePlayerContext::selectedSlot() const {
    return 0;
}

void GamePlayerContext::selectSlot(int) {}

baritone::HotbarStack GamePlayerContext::hotbar(int) const {
    return {};
}

int GamePlayerContext::countItem(std::string_view) const {
    return 0;
}

std::vector<baritone::EntityInfo> GamePlayerContext::entities() const {
    return {};
}

bool GamePlayerContext::startOrContinueBreaking(const BlockPos&, baritone::Direction) {
    return false;
}

void GamePlayerContext::stopBreaking() {}

void GamePlayerContext::useItemOn(const BlockPos&, baritone::Direction, const Vec3d&) {}

void GamePlayerContext::swingArm() {}

void GamePlayerContext::refreshWorld(const BlockPos& center, int radius) {
    WorldTracker::get().refreshAround(center, radius);
}

bool GamePlayerContext::isFlying() const {
    const auto* value = abilities();
    return value && (value->data[offsets::MovementAbilities_flags] & 1u) != 0;
}

bool GamePlayerContext::isCreative() const {
    if (!registry_ || !haveEntity_) return false;
    const auto* type = registryOf(registry_).tryGet<sdk::ActorGameTypeComponent>(entity_);
    return type && type->gameType == 1;
}

int GamePlayerContext::foodLevel() const {
    return 20;
}

float GamePlayerContext::health() const {
    return 20;
}

int GamePlayerContext::effectAmplifier(std::string_view) const {
    return -1;
}

}  // namespace baritone::platform
