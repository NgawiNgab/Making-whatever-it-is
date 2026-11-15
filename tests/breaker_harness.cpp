// Tests the part of the break hook that can be tested without a game: it must
// refuse rather than guess.
//
// The real GameMode::_sendTryDestroyBlock is only reachable on device. What is
// checkable here is the safety behaviour around it - an unresolved signature must
// not become a wild jump into a 342 MB binary.

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
    auto& breaker = BlockBreaker::get();

    std::printf("\n== break hook safety ==\n");
    check(!breaker.ready(), "starts unresolved");

    const bool empty = breaker.resolve("", "libminecraftpe.so");
    check(!empty, "empty signature refuses to resolve");
    check(!breaker.ready(), "still unresolved after refusing");
    check(!breaker.lastError().empty(), "says why it refused");

    const std::uint64_t before = breaker.attempts();
    const bool called = breaker.call(12, -59, 8, 1);
    check(!called, "calling while unresolved does not jump");
    check(breaker.attempts() == before + 1, "the attempt is still counted");

    breaker.setGameMode(reinterpret_cast<void*>(0x1000));
    check(!breaker.call(12, -59, 8, 1), "setGameMode alone does not make it safe to call");
    check(breaker.refusals() >= 2, "refusals are counted for the log");

    std::printf("\n== result ==\n%s\n",
                gFailures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return gFailures == 0 ? 0 : 1;
}
