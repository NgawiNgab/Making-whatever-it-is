#pragma once
// Breaking a block, the way the game does it.
//
// The core decides to break (Input::ClickLeft) but this port has nothing that turns
// that into a game call: PlayerContext::startOrContinueBreaking is a stub and the
// input layer only forwards movement keys. On top of that, a synthetic click has to
// be aimed, and that means writing camera pitch, which InputOverride never did.
//
// So this goes around the input layer and calls the game's own destroy path:
//
//     GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char face)
//
// Name and signature confirmed from a mangled lambda:
//   ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE  ->  BlockPos const&, unsigned char
// It is non-virtual and GameMode has no vtable, so this is an address hook, not a
// vtable slot.
//
// Not yet active: the address is not known (see memory/Signatures.h and
// docs/features/ZAPH_QUEUE.md). Everything around the address is written and
// compiled, so filling in the signature is the only remaining step.
//
// Safety rules this class follows, because a wrong address here would be a crash
// rather than a bad path:
//   * resolve() does nothing unless the signature is non-empty and resolves to
//     exactly one function
//   * call() refuses when unresolved, and counts refusals so the log says why
//   * the game-mode object is a plain pointer that is never dereferenced here; it
//     is captured, not guessed

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

    // Resolve the signature. Returns false and logs the reason if it cannot.
    // `moduleName` is libminecraftpe.so; kept explicit so the unit test can stub it.
    bool resolve(const char* signature, const char* moduleName);

    bool ready() const { return fn_ != nullptr; }
    std::uintptr_t address() const { return address_; }

    // The game-mode object. Captured from gameplay, never invented.
    void setGameMode(void* gameMode) { gameMode_ = gameMode; }
    void* gameMode() const { return gameMode_; }

    // Ask the game to destroy the block at (x, y, z) on `face`.
    // Returns false if not resolved, no game mode, or the game said no.
    bool call(int x, int y, int z, std::uint8_t face);

    // For logging and the tests.
    std::uint64_t attempts() const { return attempts_; }
    std::uint64_t refusals() const { return refusals_; }
    const std::string& lastError() const { return lastError_; }

private:
    BlockBreaker() = default;

    SendTryDestroyBlockFn fn_ = nullptr;
    std::uintptr_t address_ = 0;
    void* gameMode_ = nullptr;
    std::uint64_t attempts_ = 0;
    std::uint64_t refusals_ = 0;
    std::string lastError_;
};

}  // namespace baritone::platform
