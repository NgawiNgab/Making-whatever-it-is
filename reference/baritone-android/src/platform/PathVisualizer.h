#pragma once
#include <mutex>
#include <vector>

#include "baritone/utils/BlockPos.h"

namespace baritone::platform {

class PathVisualizer {
public:
    struct Snapshot {
        bool active = false;
        std::vector<baritone::BlockPos> current;
        std::vector<baritone::BlockPos> next;
    };

    PathVisualizer();

    void update(const std::vector<baritone::BlockPos>& current,
                const std::vector<baritone::BlockPos>& next, const baritone::Vec3d& eye,
                const baritone::Vec3d& anchor, const baritone::Vec3d& cameraForward,
                bool active, float fovCompensation = 0.0f);
    void clear();
    void shutdown();

    [[nodiscard]] Snapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    bool active_ = false;
    bool logged_ = false;
    float verticalFov_ = 60.0f;
    float lastFovCompensation_ = 0.0f;
    float lastForwardX_ = 0.0f;
    float lastForwardZ_ = 1.0f;
    std::vector<baritone::BlockPos> current_;
    std::vector<baritone::BlockPos> next_;
};

}  // namespace baritone::platform
