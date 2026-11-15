#include "platform/Paths.h"

namespace paths {
namespace {

std::filesystem::path g_dataDir;

}  // namespace

void setDataDir(std::filesystem::path dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    g_dataDir = std::move(dir);
}

const std::filesystem::path& dataDir() {
    return g_dataDir;
}

}  // namespace paths
