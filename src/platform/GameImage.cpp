#include "GameImage.hpp"

#include <pl/Logger.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace camface::platform {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

// Module discovery follows the SDK's own method (preloader src/pl/memory/Signature.cpp
// getModuleInfo): read /proc/self/maps to find the ranges belonging to the library.
//
// The first version of this file used dlinfo(handle, RTLD_DI_LINKMAP, ...) to get the
// load bias. That is a glibc extension: Android's <dlfcn.h> does not define
// RTLD_DI_LINKMAP, and the build fails with "use of undeclared identifier". A host
// stub cannot catch that, which is why it reached CI. /proc/self/maps exists on both,
// and is what the reference chain already relies on.
struct Region {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
};

struct ParsedMapLine {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    std::uintptr_t fileOffset = 0;
    bool executable = false;
    bool readable = false;
    bool matches = false;
};

// Parses one /proc/self/maps line, e.g.
//   7f3c1a0000-7f3c2b0000 r-xp 00000000 fd:01 1234 /data/app/.../libminecraftpe.so
bool parseMapLine(char const* line, std::string_view libraryName, ParsedMapLine& out) {
    unsigned long long start = 0;
    unsigned long long end = 0;
    unsigned long long fileOffset = 0;
    char perms[5] = {0};
    int consumed = 0;
    if (std::sscanf(line, "%llx-%llx %4s %llx %*s %*s %n", &start, &end, perms, &fileOffset,
                    &consumed) < 4) {
        return false;
    }

    const char* path = line + consumed;
    while (*path == ' ') ++path;

    // The path must *end* with the library name: the same string can appear as a
    // prefix of a longer path (e.g. a .so inside a different directory).
    const std::size_t pathLength = std::strlen(path);
    if (pathLength == 0) return false;
    // Trim the trailing newline that fgets keeps.
    std::string_view pathView(path, pathLength - (path[pathLength - 1] == '\n' ? 1 : 0));
    if (pathView.size() < libraryName.size()) return false;
    if (pathView.substr(pathView.size() - libraryName.size()) != libraryName) return false;

    out.start = static_cast<std::uintptr_t>(start);
    out.end = static_cast<std::uintptr_t>(end);
    out.fileOffset = static_cast<std::uintptr_t>(fileOffset);
    out.readable = perms[0] == 'r';
    out.executable = perms[2] == 'x';
    out.matches = true;
    return end > start;
}

}  // namespace

bool gameLibraryLoaded() {
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) return false;

    bool found = false;
    char line[4096];
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        ParsedMapLine parsed;
        if (parseMapLine(line, "libminecraftpe.so", parsed)) {
            found = true;
            break;
        }
    }
    std::fclose(maps);
    return found;
}

bool GameImageSource::attach(std::string_view libraryName) {
    if (m_attached) return true;
    if (libraryName.empty()) libraryName = "libminecraftpe.so";

    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        log().warn("game image: /proc/self/maps unreadable");
        return false;
    }

    // Pick the executable, readable mapping with the smallest file offset: that is the
    // first PT_LOAD, i.e. the segment whose start is the module base. Later mappings
    // (and the .data/.bss ones) have larger offsets or no 'x' permission.
    bool found = false;
    ParsedMapLine best;
    char line[4096];
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        ParsedMapLine parsed;
        if (!parseMapLine(line, libraryName, parsed)) continue;
        if (!parsed.executable || !parsed.readable) continue;
        if (!found || parsed.fileOffset < best.fileOffset) {
            best = parsed;
            found = true;
        }
    }
    std::fclose(maps);

    if (!found) {
        log().warn("game image: {} is not mapped", libraryName);
        return false;
    }

    // For the first PT_LOAD, p_vaddr == p_offset == 0 on Android, so the segment's
    // runtime start is the load bias and mapping an RVA is a straight offset from it.
    m_base = best.start - best.fileOffset;
    m_segmentStart = best.start;
    m_segmentVaddr = best.fileOffset;  // vaddr of the segment start, in RVA space
    m_size = best.end - best.start;
    m_attached = true;

    log().info("game image attached: base=0x{:x} segmentStart=0x{:x} size={}", m_base,
               m_segmentStart, m_size);
    return true;
}

bool GameImageSource::read(std::size_t offset, std::span<std::uint8_t> out) {
    if (!m_attached || out.empty()) return false;
    if (offset >= m_size || m_size - offset < out.size()) return false;

    const auto source =
        reinterpret_cast<const std::uint8_t*>(toRuntime(static_cast<std::uintptr_t>(offset)));
    std::memcpy(out.data(), source, out.size());
    return true;
}

}  // namespace camface::platform
