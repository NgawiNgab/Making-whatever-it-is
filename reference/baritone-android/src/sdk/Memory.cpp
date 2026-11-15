#include "sdk/Memory.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace mem {
namespace {

struct Region {
    std::uintptr_t start;
    std::uintptr_t end;
};

std::vector<Region> loadRegions() {
    std::vector<Region> regions;
    std::FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return regions;

    char line[512];
    while (std::fgets(line, sizeof(line), maps)) {
        unsigned long start = 0;
        unsigned long end = 0;
        char perms[8]{};
        if (std::sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3) continue;
        if (perms[0] != 'r') continue;
        regions.push_back({static_cast<std::uintptr_t>(start),
                           static_cast<std::uintptr_t>(end)});
    }
    std::fclose(maps);
    return regions;
}

bool contains(const std::vector<Region>& regions, std::uintptr_t address,
              std::uintptr_t last) {
    for (const auto& region : regions) {
        if (address >= region.start && last <= region.end) return true;
    }
    return false;
}

std::mutex& regionsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<Region>& regionCache() {
    static std::vector<Region> cached = loadRegions();
    return cached;
}

}  // namespace

bool isReadable(const void* ptr, std::size_t size) {
    if (!ptr || size == 0) return false;
    const auto address = addressOf(ptr);
    if (address < 0x1000) return false;
    const auto last = address + size;
    if (last < address) return false;

    std::lock_guard lock(regionsMutex());
    auto& cached = regionCache();
    if (contains(cached, address, last)) return true;

    cached = loadRegions();
    return contains(cached, address, last);
}

bool moduleRange(std::string_view moduleName, Range& out) {
    std::FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return false;

    const std::string needle(moduleName);
    bool found = false;
    unsigned long start = 0;
    unsigned long end = 0;
    char perms[8]{};
    char line[512];

    while (std::fgets(line, sizeof(line), maps)) {
        if (line[0] == '\0' || !std::strstr(line, needle.c_str())) continue;
        if (std::sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3) continue;
        if (perms[0] != 'r') continue;
        if (!found) {
            out.start = static_cast<std::uintptr_t>(start);
            out.end = static_cast<std::uintptr_t>(end);
            found = true;
        } else {
            if (start < out.start) out.start = static_cast<std::uintptr_t>(start);
            if (end > out.end) out.end = static_cast<std::uintptr_t>(end);
        }
    }

    std::fclose(maps);
    return found;
}

}  // namespace mem
