#pragma once
#include <filesystem>

namespace paths {

// Directory owned by the mod (preloader data dir). Set once before the bot starts.
void setDataDir(std::filesystem::path dir);
const std::filesystem::path& dataDir();

}  // namespace paths
