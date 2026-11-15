#pragma once

// Byte signatures recovered from libminecraftpe.so 1.26.62.1. Every pattern below was
// checked offline against the .text segment and matches exactly one function.
namespace signatures {

inline constexpr const char* ClientInputUpdateSystemInternal_tick =
    "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 "
    "FD 03 00 91 FF 03 0C D1 59 D0 3B D5 F3 03 00 AA 28 17 40 F9 A8 03 1F F8 "
    "08 C0 52 39 ? ? ? ? 00 E4 00 6F 28 00 40 F9";

inline constexpr const char* BlockSourceGetBlock =
    "FF 03 01 D1 FD 7B 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 43 00 91 56 D0 3B D5 "
    "C8 16 40 F9 E8 07 00 F9 28 04 40 B9 09 84 C0 79 1F 01 09 6B ? ? ? ? "
    "09 80 C0 79 F3 03 00 AA 1F 01 09 6B ? ? ? ?";

inline constexpr const char* BlockSourceIsSolidBlockingBlock =
    "09 00 40 F9 23 08 40 B9 28 08 40 29 24 DD 40 F9 E1 03 08 2A 80 00 1F D6 "
    "FF 03 01 D1 FD 7B 02 A9 F3 1B 00 F9 FD 83 00 91 53 D0 3B D5 68 16 40 F9 "
    "A8 83 1F F8 08 00 40 F9 20 00 40 FD 29 08 40 B9";

inline constexpr const char* BlockSourceGetBiome =
    "FF 03 01 D1 FD 7B 01 A9 F5 13 00 F9 F4 4F 03 A9 FD 43 00 91 55 D0 3B D5 "
    "F3 03 00 AA F4 03 01 AA A8 16 40 F9 E8 07 00 F9 08 00 40 F9 08 9D 40 F9 "
    "00 01 3F D6 ? ? ? ? 89 06 40 B9 68 86 C0 79";

inline constexpr const char* ActorSetRotation =
    "08 1C 41 F9 E0 03 01 AA 02 21 00 91 E1 03 08 AA 09 A1 3D 17";

inline constexpr const char* InputHandler_tick =
    "E9 23 B9 6D FD 7B 01 A9 FC 6F 02 A9 FA 67 03 A9 F8 5F 04 A9 "
    "F6 57 05 A9 F4 4F 06 A9 FD 43 00 91 FF 03 07 D1 E4 97 00 B9";

inline constexpr const char* InputEventQueue_enqueueDirection =
    "E9 23 BB 6D FD 7B 01 A9 F7 13 00 F9 F6 57 03 A9 F4 4F 04 A9 "
    "FD 43 00 91 09 40 20 1E 00 80 C0 3C 2B 09 80 52 0C 00 80 92 "
    "28 40 20 1E F5 03 04 2A";

inline constexpr const char* moduleName = "libminecraftpe.so";

}  // namespace signatures
