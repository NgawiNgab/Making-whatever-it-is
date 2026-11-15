#include "platform/PathVisualizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <pl/ModMenu.hpp>

#include "platform/Log.h"

namespace baritone::platform {
namespace {

constexpr float kPanelHeightFraction = 0.30f;
constexpr float kPanelMargin = 24.0f;
constexpr float kPanelTopFraction = 0.28f;
constexpr double kRadiusBlocks = 20.0;
constexpr double kBreakDistance = kRadiusBlocks * 3.0;
constexpr std::size_t kMaxPoints = 64;
constexpr float kLineWidth = 4.0f;
constexpr float kBorderWidth = 2.0f;
constexpr float kPlayerRadius = 5.0f;
constexpr float kGoalRadius = 6.0f;
constexpr float kHeadingLength = 16.0f;

struct Panel {
    float cx = 0.0f;
    float cy = 0.0f;
    float half = 0.0f;
    double scale = 1.0;
    double fx = 0.0;
    double fz = 1.0;
    double rx = -1.0;
    double rz = 0.0;
    double px = 0.0;
    double pz = 0.0;
};

struct Local {
    double x = 0.0;
    double y = 0.0;
};

std::uint32_t color(std::uint32_t rgb) {
    return 0xFF000000u | rgb;
}

Local toLocal(const Panel& panel, double wx, double wz) {
    const double dx = wx - panel.px;
    const double dz = wz - panel.pz;
    return {dx * panel.rx + dz * panel.rz, dx * panel.fx + dz * panel.fz};
}

bool clipSegment(Local& a, Local& b, double limit) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x + limit, limit - a.x, a.y + limit, limit - a.y};
    double t0 = 0.0;
    double t1 = 1.0;
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) {
            if (r > t1) return false;
            t0 = std::max(t0, r);
        } else {
            if (r < t0) return false;
            t1 = std::min(t1, r);
        }
    }
    const Local start = a;
    a = {start.x + t0 * dx, start.y + t0 * dy};
    b = {start.x + t1 * dx, start.y + t1 * dy};
    return true;
}

void appendLine(std::vector<pl::modmenu::DrawCommand>& commands, float x0, float y0, float x1,
                float y1, float width, std::uint32_t argb) {
    pl::modmenu::DrawCommand command;
    command.type = pl::modmenu::DrawCommandType::Line;
    command.x = x0;
    command.y = y0;
    command.w = x1 - x0;
    command.h = y1 - y0;
    command.color = argb;
    command.size = width;
    commands.push_back(command);
}

void appendCircle(std::vector<pl::modmenu::DrawCommand>& commands, float x, float y,
                  float radius, std::uint32_t argb) {
    pl::modmenu::DrawCommand command;
    command.type = pl::modmenu::DrawCommandType::CircleFilled;
    command.x = x;
    command.y = y;
    command.color = argb;
    command.size = radius;
    commands.push_back(command);
}

float screenX(const Panel& panel, double lx) {
    return panel.cx + static_cast<float>(lx * panel.scale);
}

float screenY(const Panel& panel, double ly) {
    return panel.cy - static_cast<float>(ly * panel.scale);
}

void appendSegment(std::vector<pl::modmenu::DrawCommand>& commands, const Panel& panel,
                   Local a, Local b, std::uint32_t argb) {
    if (!clipSegment(a, b, kRadiusBlocks)) return;
    appendLine(commands, screenX(panel, a.x), screenY(panel, a.y), screenX(panel, b.x),
               screenY(panel, b.y), kLineWidth, argb);
}

bool appendPath(std::vector<pl::modmenu::DrawCommand>& commands, const Panel& panel,
                const std::vector<baritone::BlockPos>& path, bool anchored,
                std::uint32_t argb) {
    if (path.empty()) return true;
    const std::size_t limit = std::min(path.size(), kMaxPoints);
    Local previous = anchored ? Local{0.0, 0.0}
                              : toLocal(panel, path[0].x + 0.5, path[0].z + 0.5);
    for (std::size_t i = anchored ? 0 : 1; i < limit; ++i) {
        const Local current = toLocal(panel, path[i].x + 0.5, path[i].z + 0.5);
        appendSegment(commands, panel, previous, current, argb);
        previous = current;
        if (std::max(std::abs(current.x), std::abs(current.y)) > kBreakDistance) return false;
    }
    return limit == path.size();
}

void appendGoalMarker(std::vector<pl::modmenu::DrawCommand>& commands, const Panel& panel,
                      const baritone::BlockPos& goal) {
    Local local = toLocal(panel, goal.x + 0.5, goal.z + 0.5);
    const double extent = std::max(std::abs(local.x), std::abs(local.y));
    if (extent > kRadiusBlocks) {
        const double factor = kRadiusBlocks / extent;
        local = {local.x * factor, local.y * factor};
    }
    appendCircle(commands, screenX(panel, local.x), screenY(panel, local.y), kGoalRadius,
                 color(0xFF4040));
}

void appendBorder(std::vector<pl::modmenu::DrawCommand>& commands, const Panel& panel) {
    const float l = panel.cx - panel.half;
    const float r = panel.cx + panel.half;
    const float t = panel.cy - panel.half;
    const float b = panel.cy + panel.half;
    const std::uint32_t argb = 0xB0FFFFFFu;
    appendLine(commands, l, t, r, t, kBorderWidth, argb);
    appendLine(commands, r, t, r, b, kBorderWidth, argb);
    appendLine(commands, r, b, l, b, kBorderWidth, argb);
    appendLine(commands, l, b, l, t, kBorderWidth, argb);
}

}  // namespace

PathVisualizer::PathVisualizer() = default;

void PathVisualizer::update(const std::vector<baritone::BlockPos>& current,
                            const std::vector<baritone::BlockPos>& next,
                            const baritone::Vec3d&, const baritone::Vec3d& anchor,
                            const baritone::Vec3d& cameraForward, bool active, float) {
    if (!active) {
        clear();
        return;
    }

    {
        std::lock_guard lock(mutex_);
        current_ = current;
        next_ = next;
        active_ = true;
    }

    const auto surface = pl::modmenu::getHudSurfaceSize();
    if (surface.width < 2.0f || surface.height < 2.0f) return;

    double fx = cameraForward.x;
    double fz = cameraForward.z;
    const double length = std::sqrt(fx * fx + fz * fz);
    if (std::isfinite(length) && length > 1.0e-3) {
        fx /= length;
        fz /= length;
        lastForwardX_ = static_cast<float>(fx);
        lastForwardZ_ = static_cast<float>(fz);
    } else {
        fx = lastForwardX_;
        fz = lastForwardZ_;
    }

    const float size = surface.height * kPanelHeightFraction;
    Panel panel;
    panel.half = size * 0.5f;
    panel.cx = surface.width - kPanelMargin - panel.half;
    panel.cy = surface.height * kPanelTopFraction + panel.half;
    panel.scale = static_cast<double>(panel.half) / kRadiusBlocks;
    panel.fx = fx;
    panel.fz = fz;
    panel.rx = -fz;
    panel.rz = fx;
    panel.px = anchor.x;
    panel.pz = anchor.z;

    if (!logged_) {
        logger::info("RENDER minimap surface={:.0f}x{:.0f} panel={:.0f} radius={:.0f} "
                     "current={} next={}",
                     surface.width, surface.height, size, kRadiusBlocks, current.size(),
                     next.size());
        logged_ = true;
    }

    std::vector<pl::modmenu::DrawCommand> commands;
    commands.reserve(kMaxPoints * 2 + 16);
    appendBorder(commands, panel);
    appendPath(commands, panel, next, false, color(0xFFD24A));
    const bool complete = appendPath(commands, panel, current, true, color(0x00FF55));
    if (complete && !current.empty()) appendGoalMarker(commands, panel, current.back());
    appendCircle(commands, panel.cx, panel.cy, kPlayerRadius, color(0xFFFFFF));
    appendLine(commands, panel.cx, panel.cy, panel.cx, panel.cy - kHeadingLength, kLineWidth,
               color(0xFFFFFF));

    pl::modmenu::submitDrawCommands("baritone", commands);
}

void PathVisualizer::clear() {
    {
        std::lock_guard lock(mutex_);
        if (!active_) return;
        active_ = false;
        logged_ = false;
        current_.clear();
        next_.clear();
    }
    pl::modmenu::submitDrawCommands("baritone", {});
}

void PathVisualizer::shutdown() {
    clear();
}

PathVisualizer::Snapshot PathVisualizer::snapshot() const {
    std::lock_guard lock(mutex_);
    return {active_, current_, next_};
}

}  // namespace baritone::platform
