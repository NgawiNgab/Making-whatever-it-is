#include "platform/BlockBreaker.h"

#include "platform/Log.h"

#if defined(__ANDROID__)
#include <pl/memory/Signature.hpp>
#endif

namespace baritone::platform {

namespace {

// BlockPos is three ints. Defined locally so the unit test can build this file
// without the game headers; the layout is what the game passes on the stack.
struct GameBlockPos {
    int x = 0;
    int y = 0;
    int z = 0;
};
static_assert(sizeof(GameBlockPos) == 0xC, "BlockPos must be 12 bytes");

}  // namespace

BlockBreaker& BlockBreaker::get() {
    static BlockBreaker instance;
    return instance;
}

bool BlockBreaker::acceptAddress(std::uintptr_t address, ExecutableRange range) {
    if (!address) {
        lastError_ = "address is null";
        return false;
    }
    // Refuse anything outside the module's executable segment. Catching this here is
    // the difference between "no mining today" and a SIGSEGV inside the game.
    if (range.end > range.start && (address < range.start || address >= range.end)) {
        lastError_ = "address is outside the executable range";
        logger::error("BREAK refusing 0x{:X}: not in 0x{:X}..0x{:X}", address, range.start,
                      range.end);
        return false;
    }
    address_ = address;
    fn_ = reinterpret_cast<SendTryDestroyBlockFn>(address);
    lastError_.clear();
    return true;
}

bool BlockBreaker::resolve(const char* signature, const char* moduleName) {
    if (fn_) return true;

    if (!signature || signature[0] == '\0') {
        lastError_ = "no signature for GameMode::_sendTryDestroyBlock yet";
        logger::warn("BREAK cannot hook: {}", lastError_);
        return false;
    }

#if defined(__ANDROID__)
    const std::uintptr_t resolved =
        pl::memory::resolveSignature(signature, moduleName ? moduleName : "libminecraftpe.so");
    if (!resolved) {
        lastError_ = "signature did not match";
        logger::warn("BREAK cannot hook: {}", lastError_);
        return false;
    }
    // The resolver returns a live address, so the range check is done by the caller
    // that knows the module bounds; here we trust the resolver but record the value.
    absolute_ = false;
    address_ = resolved;
    fn_ = reinterpret_cast<SendTryDestroyBlockFn>(resolved);
    lastError_.clear();
    logger::info("BREAK GameMode::_sendTryDestroyBlock at 0x{:X}", resolved);
    return true;
#else
    (void)moduleName;
    lastError_ = "host build: no game module to resolve against";
    logger::info("BREAK {}", lastError_);
    return false;
#endif
}

bool BlockBreaker::resolveAbsolute(std::uintptr_t address, std::uintptr_t imageStart,
                                   ExecutableRange range) {
    if (fn_) return true;
    if (!acceptAddress(address, range)) {
        logger::warn("BREAK cannot hook: {}", lastError_);
        return false;
    }
    absolute_ = true;
    // Say it every time. A hand-measured offset stops being correct the moment the
    // game is updated, and the symptom of that is a crash, so make it visible.
    logger::warn(
        "BREAK using hand-measured address 0x{:X} (game image at 0x{:X}, offset 0x{:X}). "
        "This breaks when the game updates - replace it with a signature.",
        address, imageStart, address - imageStart);
    return true;
}

bool BlockBreaker::call(int x, int y, int z, std::uint8_t face) {
    ++attempts_;
    if (!fn_) {
        ++refusals_;
        return false;
    }
    if (!gameMode_) {
        ++refusals_;
        if (refusals_ <= 3 || refusals_ % 100 == 0) {
            logger::warn("BREAK no game mode captured yet ({} refusals)", refusals_);
        }
        return false;
    }
    const GameBlockPos pos{x, y, z};
    const bool ok = fn_(gameMode_, &pos, face);
    if (!ok) {
        ++refusals_;
        if (refusals_ <= 3 || refusals_ % 100 == 0) {
            logger::info("BREAK game refused block {} {} {} face {}", x, y, z,
                         static_cast<int>(face));
        }
    }
    return ok;
}

}  // namespace baritone::platform
