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

constexpr SignatureEntry kSignatures[] = {
    // Verified: matches 1, function @ rva 0xaf60090, 1408 bytes, single ret @ 0xaf60610.
    // Prefix `?? ?? ?? D1` is `sub sp, sp, #imm`; the pattern's fixed bytes `56 D0 3B D5`
    // are the `mrs x22, tpidr_el0` that makes this function self-identifying.
    {"LocalPlayerApplyTurnDelta", kTurnDeltaPattern, 0x400, "fd7bbda9"},
    {"ClientInstanceGetLocalPlayer", kClientInstanceLocalPlayerPattern, 0x40, "ff4300d1"},
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
