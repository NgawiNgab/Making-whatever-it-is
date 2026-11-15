#pragma once

#include <cstdint>
#include <string_view>

namespace camface {

// Parsed by scripts/package_levipack.py to build manifest.json. The parser wants
// `inline constexpr std::string_view <Key> = "...";` on one line per key, so do
// not reformat these four lines.
inline constexpr std::string_view Name = "Camera Facing";
inline constexpr std::string_view Author = "NgawiNgab";
inline constexpr std::string_view Description = "Observe and snap the camera to a chosen yaw.";
inline constexpr std::string_view Version = "0.1.0";

inline constexpr std::string_view RuntimeLibrary = "libCameraFacing.so";

// The Minecraft build this binary's data/ tables were verified against. Printed at
// startup so a mismatched install is visible immediately rather than inferred
// from broken behaviour.
inline constexpr std::string_view TargetMinecraft = "1.26.62";

}  // namespace camface
