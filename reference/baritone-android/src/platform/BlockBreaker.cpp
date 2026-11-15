#include "platform/BlockBreaker.h"

#include <cstring>

#include "platform/Log.h"

#if defined(__ANDROID__)
#include <pl/memory/Signature.hpp>
#endif

namespace baritone::platform {

namespace {

// BlockPos is three ints. Defined locally rather than pulled from sdk/ so the unit
// test can build this file without the game headers; the layout is the game's, and
// the static assert is the guard.
struct GameBlockPos {
    int x = 0;
    int y = 0;
    int z = 0;
};
static_assert(sizeof(GameBlockPos) == 0xC, "BlockPos passed on the stack must be 12 bytes");

}  // namespace

BlockBreaker& BlockBreaker::get() {
    static BlockBreaker instance;
    return instance;
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
    address_ = resolved;
    fn_ = reinterpret_cast<SendTryDestroyBlockFn>(resolved);
    lastError_.clear();
    logger::info("BREAK GameMode::_sendTryDestroyBlock at 0x{:X}", resolved);
    return true;
#else
    // Host build: no game to resolve against. Say so instead of pretending.
    (void)moduleName;
    lastError_ = "host build: no game module to resolve against";
    logger::info("BREAK {}", lastError_);
    return false;
#endif
}

bool BlockBreaker::call(int x, int y, int z, std::uint8_t face) {
    ++attempts_;
    if (!fn_) {
        ++refusals_;
        // Not an error every tick: the bot may want to break while unresolved.
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
    if (!ok && (refusals_ <= 3 || ++refusals_ % 100 == 0)) {
        logger::info("BREAK game refused block {} {} {} face {}", x, y, z,
                     static_cast<int>(face));
    }
    return ok;
}

}  // namespace baritone::platform
