#include "GameImage.hpp"

#include <pl/Logger.hpp>

#include <dlfcn.h>
#include <link.h>

#include <cstring>
#include <span>
#include <string>

namespace camface::platform {
namespace {

auto& log() { return pl::log::Logger::getOrCreate("CameraFacing"); }

struct SegmentInfo {
    std::uintptr_t start = 0;   // runtime (load-biased) address of the segment
    std::uintptr_t fileOffset = 0;
    std::uintptr_t fileSize = 0;
    std::uintptr_t vaddr = 0;
};

struct Probe {
    SegmentInfo text;
    bool found = false;
};

}  // namespace

bool gameLibraryLoaded() {
    void* handle = dlopen("libminecraftpe.so", RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) return false;
    dlclose(handle);
    return true;
}

bool GameImageSource::attach(std::string_view libraryName) {
    if (m_attached) return true;

    const std::string name(libraryName);
    void* handle = dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) return false;

    // Link map we just loaded: this is the authoritative base address.
    struct link_map* map = nullptr;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) != 0 || map == nullptr) {
        dlclose(handle);
        return false;
    }
    m_base = static_cast<std::uintptr_t>(map->l_addr);

    // Find *this object's* executable segment. Matching on dlpi_addr is what makes
    // this unambiguous: several libraries in the process are executable, but only
    // the game's has the base we just read out of the link map.
    Probe probe;
    probe.text.start = m_base;  // used by the callback to select this object
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t size, void* data) -> int {
            auto* p = static_cast<Probe*>(data);
            (void)size;
            if (info->dlpi_addr != static_cast<ElfW(Addr)>(p->text.start)) return 0;

            std::uintptr_t bestSize = 0;
            SegmentInfo best;
            for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
                const ElfW(Phdr)& header = info->dlpi_phdr[i];
                if (header.p_type != PT_LOAD) continue;
                if ((header.p_flags & PF_X) == 0) continue;
                if (static_cast<std::uintptr_t>(header.p_memsz) > bestSize) {
                    bestSize = header.p_memsz;
                    best.start = static_cast<std::uintptr_t>(info->dlpi_addr) + header.p_vaddr;
                    best.fileOffset = header.p_offset;
                    best.fileSize = header.p_filesz;
                    best.vaddr = header.p_vaddr;
                }
            }
            if (bestSize != 0) {
                p->text = best;
                p->text.start = static_cast<std::uintptr_t>(info->dlpi_addr);
                p->text.vaddr = best.vaddr;
                p->text.fileSize = best.fileSize;
                p->found = true;
                return 1;
            }
            return 0;
        },
        &probe);

    dlclose(handle);

    if (!probe.found || probe.text.fileSize == 0) {
        log().warn("game image: no executable segment found");
        return false;
    }

    // The pattern scan operates in file-image space (RVAs, as filed in
    // evidence/T-CAM1). Reading is done from the in-memory segment, so translate:
    //   runtimeAddr = segment.start + (rva - segment.vaddr)
    // and only RVAs inside this segment are addressable.
    m_segmentStart = probe.text.start;
    m_segmentVaddr = probe.text.vaddr;
    m_size = probe.text.fileSize;
    m_attached = true;

    log().info("game image attached: base=0x{:x} segmentStart=0x{:x} textVaddr=0x{:x} size={}",
               m_base, m_segmentStart, m_segmentVaddr, m_size);
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
