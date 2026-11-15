// Build data for MCPE 1.26.62, arm64-v8a.
//
// PROVENANCE — every value below traces to a filed artifact, not to a guess:
//   evidence/T-CAM1/zaphkiel_out.txt  (run 2026-11-15, lib 342,231,392 B, 7 segments)
//   docs/EVIDENCE_LOG.md              (findings F-CAM-01 .. F-CAM-15)
//
// Library identity: size matches the filed run exactly. A hash is still missing,
// so a wrong-but-same-size image is caught only by the structural checks below,
// not by the identity check. If those checks fail at runtime, the mod reports
// Rejected instead of hooking — which is the intended failure mode.

#include <camfacesdk/BuildData.hpp>

namespace camfacesdk::data {
namespace {

using namespace camfacesdk;

// F-CAM-02: 56-byte pattern, exactly 1 match on this build. Kept verbatim as it
// was verified — do not "improve" it without re-running sig<> and recording the count.
constexpr std::string_view kTurnDeltaPattern =
    "?? ?? ?? D1 ?? ?? ?? FD ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 "
    "56 D0 3B D5 F3 03 00 AA F4 03 01 AA ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? F9";

// F-CAM-08: INFERRED, not confirmed. Present so the feature layer can gate on it,
// never used to hook. Kept out of the hook path deliberately.
constexpr std::string_view kClientInstanceLocalPlayerPattern =
    "?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? F9 ?? ?? ?? 91 53 D0 3B D5 E8 03 00 AA ?? ?? ?? 91 "
    "?? ?? ?? F9 ?? ?? ?? 91 ?? ?? ?? F8 ?? ?? ?? ?? ?? ?? ?? 91 ?? ?? ?? ?? ?? ?? ?? 36 "
    "?? ?? ?? 91 ?? ?? ?? 52 ?? ?? ?? ?? ?? ?? ?? ?? E0 03 1F AA ?? ?? ?? F9 ?? ?? ?? F8 "
    "1F 01 09 EB ?? ?? ?? 54 ?? ?? ?? A9 ?? ?? ?? F9 ?? ?? ?? 91 C0 03 5F D6 ?? ?? ?? ?? "
    "C0 03 5F D6";

// The two hooks that make a real readout possible. Both patterns are copied
// verbatim from BedrockTools src/core/memory/Signatures.cpp (SignatureId::
// ClientInstanceUpdate, SignatureId::NormalTick) -- see
// docs/BEDROCKTOOLS_REFERENCE.md for the path:line citations.
//
// Why these two:
//   ClientInstanceUpdate gives us the ClientInstance pointer, because the game
//   passes it as argument 0 to its own update function.
//   NormalTick is the per-actor tick; running *after* the original means the
//   rotation component has already been written this frame.
constexpr std::string_view kClientInstanceUpdatePattern =
    "?? ?? ?? FC ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 "
    "?? ?? ?? 91 ?? ?? ?? D1 59 D0 3B D5 ?? ?? ?? 52 F5 03 02 2A";

constexpr std::string_view kNormalTickPattern =
    "?? ?? ?? FC ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 "
    "?? ?? ?? 91 ?? ?? ?? D1 54 D0 3B D5 F3 03 00 AA ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? 39";

constexpr SignatureEntry kSignatures[] = {
    // Verified: matches 1, function @ rva 0xaf60090, 1408 bytes, single ret @ 0xaf60610.
    // Prefix `?? ?? ?? D1` is `sub sp, sp, #imm`; the pattern's fixed bytes `56 D0 3B D5`
    // are the `mrs x22, tpidr_el0` that makes this function self-identifying.
    {"LocalPlayerApplyTurnDelta", kTurnDeltaPattern, 0x400, "fd7bbda9"},
    {"ClientInstanceGetLocalPlayer", kClientInstanceLocalPlayerPattern, 0x40, "ff4300d1"},

    // Both added 2026-11-15 after the first device run showed the readout could not
    // show a yaw at all: without a player pointer there is nothing to read. Neither
    // has been run on this build yet -- if either is Missing/Ambiguous the readout
    // says so instead of drawing a fake zero.
    {"ClientInstanceUpdate", kClientInstanceUpdatePattern, 0x400, "fc.."},
    {"NormalTick", kNormalTickPattern, 0x400, "fc.."},
};

constexpr RttiSlotEntry kRttiSlots[] = {
    // Intentionally empty: this build's client map is EnTT/ECS driven (F-CAM-10),
    // and no RTTI slot is needed for the V1 camera feature. Entries are added only
    // after a vft<>/hier<> run proves slot identity on THIS build.
};

constexpr OffsetEntry kOffsets[] = {
    // Offsets that were *verified* live will land here (T-CAM2). Until then this
    // stays empty on purpose: a speculative offset is worse than a missing one,
    // because a missing one disables a feature and a wrong one corrupts memory.
};

}  // namespace

BuildData const& build_1_26_62() {
    static BuildData const data{
        .mcVersion = "1.26.62",
        .librarySize = 342231392,
        .signatures = kSignatures,
        .signatureCount = sizeof(kSignatures) / sizeof(kSignatures[0]),
        .rttiSlots = kRttiSlots,
        .rttiSlotCount = sizeof(kRttiSlots) / sizeof(kRttiSlots[0]),
        .offsets = kOffsets,
        .offsetCount = sizeof(kOffsets) / sizeof(kOffsets[0]),
    };
    return data;
}

}  // namespace camfacesdk::data
