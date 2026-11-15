#pragma once

#include <camfacesdk/Facts.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace camface::platform {

// Locates the already-loaded game library through the engine and exposes its
// executable segment as an ImageSource.
//
// IMPORTANT: `read()` takes a **file-image offset (RVA)**, not a runtime address.
// That is deliberate — every address in evidence/ is an RVA, and keeping the
// conversion in one place prevents the two spaces being mixed up (see
// evidence/T-CAM1/README.md "Address space note").
class GameImageSource final : public camfacesdk::ImageSource {
public:
    // Attaches if the game is already mapped. Safe to call repeatedly; never
    // triggers a load of the library (RTLD_NOLOAD only).
    bool attach(std::string_view libraryName = "libminecraftpe.so");

    [[nodiscard]] bool attached() const noexcept { return m_attached; }

    // Module base (load bias). `base()` is 0: RVAs are image offsets, not
    // relocated addresses, so adding a base would be wrong.
    [[nodiscard]] std::uintptr_t base() const override { return 0; }
    [[nodiscard]] std::size_t size() const override { return m_size; }
    bool read(std::size_t offset, std::span<std::uint8_t> out) override;
    [[nodiscard]] bool get() override { return m_attached; }

    // Converts a resolved RVA into a callable runtime address.
    [[nodiscard]] std::uintptr_t toRuntime(std::uintptr_t rva) const noexcept {
        return m_segmentStart + (rva - m_segmentVaddr);
    }

private:
    std::uintptr_t m_base = 0;           // load bias (diagnostics only)
    std::uintptr_t m_segmentStart = 0;   // runtime address of the text segment
    std::uintptr_t m_segmentVaddr = 0;   // vaddr of the text segment (= RVA of its start)
    std::size_t m_size = 0;              // file size of the text segment
    bool m_attached = false;
};

// True when the engine reports the game library as loaded.
bool gameLibraryLoaded();

}  // namespace camface::platform
