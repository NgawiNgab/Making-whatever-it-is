#pragma once
// Breaking a block, the way the game does it.
//
// The core decides to break (Input::ClickLeft) but this port has nothing that turns
// that into a game call: PlayerContext::startOrContinueBreaking is a stub and the
// input layer only forwards movement keys. A synthetic click would also have to be
// aimed, and that means writing camera pitch, which InputOverride never did.
//
// So this goes around the input layer and calls the game's own destroy path:
//
//     GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char face)
//
// Confirmed from a mangled lambda symbol:
//   ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE -> BlockPos const&, unsigned char
// Non-virtual and GameMode is abstract with no vtable, so this is an address hook,
// not a vtable slot. The address is still unknown - see docs/features/ZAPH_QUEUE.md.
//
// Two ways to give it an address, in order of preference:
//   1. resolve(signature): a byte pattern, resolved on device at load.
//   2. resolveAbsolute(va): an address measured by hand. Breaks on any game update,
//      so it exists as a fallback and logs loudly when used.
//
// Safety rules, because a wrong address here is a crash and not a bad path:
//   * nothing resolves unless a signature/address was actually supplied
//   * an address outside libminecraftpe.so's executable range is refused
//   * call() refuses when unresolved, and counts refusals so the log says why

#include <cstdint>
#include <string>

namespace baritone::platform {

// The call as the mangling describes it. `face` is 0-5 (down, up, north, south,
// west, east). Returns whether the game accepted the attempt.
using SendTryDestroyBlockFn = bool (*)(void* gameMode, const void* blockPos,
                                       std::uint8_t face);

class BlockBreaker {
public:
    static BlockBreaker& get();

    // Part of the image an address must fall in to be called.
    struct ExecutableRange {
        std::uintptr_t start = 0;
        std::uintptr_t end = 0;
    };

    // Resolve a byte signature on device. Returns false and logs why on failure.
    bool resolve(const char* signature, const char* moduleName);

    // Use an address measured outside the app. `imageStart` is where the module was
    // loaded; `range` is checked before anything is called. Logs a warning every time,
    // because this is the fragile path.
    bool resolveAbsolute(std::uintptr_t address, std::uintptr_t imageStart,
                         ExecutableRange range);

    bool ready() const { return fn_ != nullptr; }
    std::uintptr_t address() const { return address_; }
    bool isAbsolute() const { return absolute_; }

    // The game-mode object. Captured from gameplay, never invented.
    void setGameMode(void* gameMode) { gameMode_ = gameMode; }
    void* gameMode() const { return gameMode_; }

    // Ask the game to destroy the block at (x, y, z) on `face`.
    bool call(int x, int y, int z, std::uint8_t face);

    std::uint64_t attempts() const { return attempts_; }
    std::uint64_t refusals() const { return refusals_; }
    const std::string& lastError() const { return lastError_; }

private:
    BlockBreaker() = default;
    bool acceptAddress(std::uintptr_t address, ExecutableRange range);

    SendTryDestroyBlockFn fn_ = nullptr;
    std::uintptr_t address_ = 0;
    bool absolute_ = false;
    void* gameMode_ = nullptr;
    std::uint64_t attempts_ = 0;
    std::uint64_t refusals_ = 0;
    std::string lastError_;
};

}  // namespace baritone::platform
