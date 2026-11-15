// Tests the break hook's safety behaviour, which is all that can be tested without
// a game. The point is that an unknown or wrong address must never become a jump
// into a 342 MB binary.

#include <cstdint>
#include <cstdio>

#include "platform/BlockBreaker.h"

namespace {
int gFailures = 0;
void check(bool ok, const char* what) {
    std::printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++gFailures;
}
}  // namespace

int main() {
    using baritone::platform::BlockBreaker;
    auto& b = BlockBreaker::get();

    // Stand-in for libminecraftpe.so's executable segment.
    constexpr std::uintptr_t kImageStart = 0x7000000000;
    constexpr std::uintptr_t kTextSize = 0x5000;
    const BlockBreaker::ExecutableRange kText{kImageStart, kImageStart + kTextSize};

    std::printf("\n== no address yet ==\n");
    check(!b.ready(), "starts unresolved");
    check(!b.resolve("", "libminecraftpe.so"), "empty signature refuses");
    check(!b.ready(), "still unresolved");
    check(!b.lastError().empty(), "says why");
    check(!b.call(12, -59, 8, 1), "call while unresolved does not jump");

    b.setGameMode(reinterpret_cast<void*>(0x1000));
    check(!b.call(12, -59, 8, 1), "setGameMode alone does not make it safe");

    std::printf("\n== bad addresses are refused ==\n");
    check(!b.resolveAbsolute(0, kImageStart, kText), "null address refused");
    check(!b.ready(), "still unresolved after null address");

    const std::uintptr_t outside = kImageStart + kTextSize + 0x1000;
    check(!b.resolveAbsolute(outside, kImageStart, kText),
          "address outside .text is refused");
    check(!b.ready(), "still unresolved after out-of-range address");

    const std::uintptr_t below = kImageStart - 1;
    check(!b.resolveAbsolute(below, kImageStart, kText), "address below the image refused");

    std::printf("\n== a plausible address is accepted, and flagged ==\n");
    const std::uintptr_t good = kImageStart + 0x1234;
    check(b.resolveAbsolute(good, kImageStart, kText), "address inside .text accepted");
    check(b.ready(), "now resolved");
    check(b.isAbsolute(), "marked as hand-measured, not signature-derived");
    check(b.address() == good, "records the address it was given");

    std::printf("\n== result ==\n%s\n",
                gFailures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return gFailures == 0 ? 0 : 1;
}
