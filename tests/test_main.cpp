// Host-side tests for the parts of this mod that do not need a device.
//
// These are the only claims about correctness that can be made without the game:
// pattern parsing, uniqueness counting, the structural checks, and the resolution
// report. Everything that touches the game is out of scope here by design.

#include <camfacesdk/BuildData.hpp>
#include <camfacesdk/Facts.hpp>
#include <camfacesdk/Pattern.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, char const* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

// Fixture image: a byte buffer that behaves like the game's text segment for the
// purposes of matching. base() is 0 because resolution works in RVA space.
class FixtureImage final : public camfacesdk::ImageSource {
public:
    explicit FixtureImage(std::size_t size) : m_bytes(size, 0x00) {}

    std::vector<std::uint8_t>& bytes() { return m_bytes; }

    [[nodiscard]] std::uintptr_t base() const override { return 0; }
    [[nodiscard]] std::size_t size() const override { return m_bytes.size(); }

    bool read(std::size_t offset, std::span<std::uint8_t> out) override {
        if (offset > m_bytes.size() || m_bytes.size() - offset < out.size()) return false;
        std::memcpy(out.data(), m_bytes.data() + offset, out.size());
        return true;
    }
    [[nodiscard]] bool get() override { return true; }

private:
    std::vector<std::uint8_t> m_bytes;
};

void write(std::vector<std::uint8_t>& image, std::size_t offset,
           std::initializer_list<std::uint8_t> bytes) {
    std::size_t i = offset;
    for (std::uint8_t byte : bytes) image[i++] = byte;
}

void testParsing() {
    std::printf("parsePattern\n");
    camfacesdk::ParsedPattern pattern;

    check(camfacesdk::parsePattern("AA BB CC", pattern), "plain hex parses");
    check(pattern.size == 3, "three tokens");
    check(pattern.tokens[0].value == 0xAA && pattern.tokens[0].mask == 0xFF, "token value/mask");

    check(camfacesdk::parsePattern("AA ?? BB", pattern), "wildcard parses");
    check(pattern.tokens[1].isWildcard(), "?? is a full wildcard");

    check(camfacesdk::parsePattern("AA ? BB", pattern), "single ? parses");
    check(pattern.tokens[1].isWildcard(), "? is a full wildcard");

    // Partial masks are accepted by the engine's parser; keep parity.
    check(camfacesdk::parsePattern("A? ?4", pattern), "partial masks parse");
    check(pattern.tokens[0].mask == 0xF0 && pattern.tokens[0].value == 0xA0, "A? mask/value");
    check(pattern.tokens[1].mask == 0x0F && pattern.tokens[1].value == 0x04, "?4 mask/value");

    check(!camfacesdk::parsePattern("", pattern), "empty rejected");
    check(!camfacesdk::parsePattern("   ", pattern), "whitespace-only rejected");
    check(!camfacesdk::parsePattern("ZZ", pattern), "non-hex rejected");
    check(!camfacesdk::parsePattern("ABC", pattern), "odd-length token rejected");
}

void testMatching() {
    std::printf("matchesAt / countMatches\n");
    std::vector<std::uint8_t> data{0x11, 0x22, 0x33, 0x11, 0x22, 0x44};

    camfacesdk::ParsedPattern pattern;
    check(camfacesdk::parsePattern("11 22 33", pattern), "pattern ok");
    check(camfacesdk::matchesAt(std::span<const std::uint8_t>(data), 0, pattern), "match at 0");
    check(!camfacesdk::matchesAt(std::span<const std::uint8_t>(data), 3, pattern), "no match at 3");
    check(!camfacesdk::matchesAt(std::span<const std::uint8_t>(data), 5, pattern),
          "out-of-range rejected");

    // Uniqueness counting is the core rule (R-1): two occurrences must be reported
    // as two, not collapsed to "found".
    camfacesdk::ParsedPattern repeated;
    check(camfacesdk::parsePattern("11 22", repeated), "repeated pattern ok");
    check(camfacesdk::countMatches(std::span<const std::uint8_t>(data), repeated) == 2,
          "counts both occurrences");

    camfacesdk::ParsedPattern unique;
    check(camfacesdk::parsePattern("22 33", unique), "unique pattern ok");
    check(camfacesdk::countMatches(std::span<const std::uint8_t>(data), unique) == 1,
          "counts exactly one");

    camfacesdk::ParsedPattern absent;
    check(camfacesdk::parsePattern("DE AD BE EF", absent), "absent pattern ok");
    check(camfacesdk::countMatches(std::span<const std::uint8_t>(data), absent) == 0,
          "absent counts zero");
}

void testResolutionRules() {
    std::printf("resolveAgainst\n");

    // Fixture layout (deliberate, so each rule is exercised by exactly one entry):
    //   0x100: FD 7B BD A9 56 D0 3B D5 AA BB   <- function A
    //   0x1F0: C0 03 5F D6                     <- A's `ret`, inside the size probe
    //   0x300: FD 7B BD A9 56 D0 3B D5 CC DD   <- function B (shares A's prologue)
    constexpr std::size_t kFunctionRva = 0x100;
    constexpr std::size_t kSecondRva = 0x300;

    FixtureImage image(0x400);
    auto& bytes = image.bytes();
    write(bytes, kFunctionRva, {0xFD, 0x7B, 0xBD, 0xA9, 0x56, 0xD0, 0x3B, 0xD5, 0xAA, 0xBB});
    write(bytes, kFunctionRva + 0xF0, {0xC0, 0x03, 0x5F, 0xD6});
    write(bytes, kSecondRva, {0xFD, 0x7B, 0xBD, 0xA9, 0x56, 0xD0, 0x3B, 0xD5, 0xCC, 0xDD});

    constexpr camfacesdk::SignatureEntry kSignatures[] = {
        // Occurs once (only function A has the AA BB tail) and the prefix matches.
        {"UniqueOk", "FD 7B BD A9 56 D0 3B D5 AA BB", 0x20, "fd7bbda9"},
        // Occurs in both functions -> must be reported ambiguous, never picked.
        {"Ambiguous", "FD 7B BD A9 56 D0 3B D5", 0x20, "fd7bbda9"},
        // Occurs once but the recorded prefix does not match the site: the
        // false-unique case that evidence F-CAM-06/07 turned up in practice.
        {"WrongPrefix", "AA BB", 0x20, "ffffffff"},
        // Not present at all.
        {"Absent", "DE AD BE EF", 0x20, ""},
    };

    camfacesdk::BuildData build{
        .mcVersion = "test",
        .librarySize = 0x400,
        .signatures = kSignatures,
        .signatureCount = sizeof(kSignatures) / sizeof(kSignatures[0]),
    };

    std::vector<camfacesdk::Fact> facts;
    camfacesdk::resolveAgainst(image, build, facts);

    auto stateOf = [&facts](char const* id) -> camfacesdk::State {
        for (auto const& fact : facts) {
            if (fact.id == id) return fact.state;
        }
        return camfacesdk::State::Unknown;
    };

    check(stateOf("UniqueOk") == camfacesdk::State::Available, "unique + prefix ok => AVAILABLE");
    check(stateOf("Ambiguous") == camfacesdk::State::Ambiguous, "two matches => AMBIGUOUS");
    check(stateOf("WrongPrefix") == camfacesdk::State::Rejected, "bad prefix => REJECTED");
    check(stateOf("Absent") == camfacesdk::State::Missing, "no match => MISSING");

    // The verified prefix must be carried on the fact: the hook compares these
    // bytes to the live site before patching (HS-3). Without them the hook can only
    // trust that the address it was handed is the one that was verified.
    for (auto const& fact : facts) {
        if (fact.id == "UniqueOk") {
            check(fact.expectPrefixLength == 4, "verified prefix length recorded");
            check(fact.expectPrefix[0] == 0xFD && fact.expectPrefix[1] == 0x7B &&
                      fact.expectPrefix[2] == 0xBD && fact.expectPrefix[3] == 0xA9,
                  "verified prefix bytes recorded");
        }
        if (fact.id == "WrongPrefix") {
            check(fact.expectPrefixLength == 0, "rejected fact carries no verified prefix");
        }
    }

    // The unique fact must report the RVA of the match, and minSize must survive
    // into the fact so the hook layer can enforce HS-1 from data, not by parsing.
    for (auto const& fact : facts) {
        if (fact.id == "UniqueOk") {
            check(fact.rva == kFunctionRva, "resolved rva is the match offset");
            check(fact.minSize == 0x20, "minSize carried into the fact");
            check(fact.matches == 1, "match count recorded");
        }
        if (fact.id == "Ambiguous") {
            check(fact.address == 0, "ambiguous fact exposes no address");
            check(fact.matches == 2, "ambiguous fact records both matches");
        }
        if (fact.id == "WrongPrefix") {
            check(fact.address == 0, "rejected fact exposes no address");
        }
    }
}

void testGateRule() {
    std::printf("FactRegistry::satisfies\n");

    // Gating is the rule that keeps a feature away from the game when its evidence
    // is missing (docs/SPEC.md FR-3), so it is tested directly rather than inferred
    // from a successful startup.
    FixtureImage image(0x400);
    write(image.bytes(), 0x100, {0xFD, 0x7B, 0xBD, 0xA9, 0x56, 0xD0, 0x3B, 0xD5, 0xAA, 0xBB});
    write(image.bytes(), 0x1F0, {0xC0, 0x03, 0x5F, 0xD6});

    constexpr camfacesdk::SignatureEntry kSignatures[] = {
        {"Present", "FD 7B BD A9 56 D0 3B D5 AA BB", 0x20, "fd7bbda9"},
        {"Gone", "DE AD BE EF", 0x20, ""},
    };
    camfacesdk::BuildData build{
        .mcVersion = "test",
        .librarySize = 0x400,
        .signatures = kSignatures,
        .signatureCount = 2,
    };

    camfacesdk::FactRegistry registry;
    registry.resolveAll(&image, build);

    constexpr camfacesdk::FactRequirement kNeedPresent[] = {{"Present", true}};
    constexpr camfacesdk::FactRequirement kNeedGone[] = {{"Gone", true}};
    constexpr camfacesdk::FactRequirement kNeedOptional[] = {{"Gone", false}};
    constexpr camfacesdk::FactRequirement kNeedBoth[] = {{"Present", true}, {"Gone", true}};

    std::vector<std::string> missing;

    check(registry.satisfies(kNeedPresent, &missing), "available required fact opens the gate");
    check(missing.empty(), "no missing facts reported");

    missing.clear();
    check(!registry.satisfies(kNeedGone, &missing), "missing required fact closes the gate");
    check(missing.size() == 1 && missing[0] == "Gone", "the closed gate names what is missing");

    missing.clear();
    check(registry.satisfies(kNeedOptional, &missing), "a missing optional fact keeps it open");
    check(missing.empty(), "optional facts are not reported as missing");

    missing.clear();
    check(!registry.satisfies(kNeedBoth, &missing), "one missing required fact closes the gate");
    check(missing.size() == 1, "only the missing required fact is reported");

    // An unknown fact id must close the gate, not be treated as satisfied.
    constexpr camfacesdk::FactRequirement kNeedUnknown[] = {{"NoSuchFact", true}};
    missing.clear();
    check(!registry.satisfies(kNeedUnknown, &missing), "unknown fact closes the gate");
    check(missing.size() == 1, "unknown fact is reported missing");
}

void testRegistryReport() {
    std::printf("FactRegistry\n");
    camfacesdk::BuildData const& build = camfacesdk::currentBuild();
    check(!build.mcVersion.empty(), "current build has a version");
    check(camfacesdk::findBuild("1.26.62") != nullptr, "1.26.62 is a supported build");
    check(camfacesdk::findBuild("0.0.0") == nullptr, "unknown build is unsupported");

    camfacesdk::FactRegistry registry;
    const bool all = registry.resolveAll(nullptr, build);
    check(!all, "no image => not all available");
    check(!registry.all().empty(), "facts were reported");

    for (auto const& fact : registry.all()) {
        check(fact.state == camfacesdk::State::Unsupported,
              "without an image every fact is UNSUPPORTED");
        check(fact.address == 0, "no address without an image");
    }

    const std::string summary = registry.summary();
    check(summary.find("unsupported") != std::string::npos, "summary names the state");

    const std::string report = registry.writeReport("");
    check(report.find("\"facts\"") != std::string::npos, "report has a facts array");
    check(report.find("\"rva\"") != std::string::npos, "report carries rvas");
    // Live addresses must never be in the report: only RVAs survive a run.
    check(report.find("0x0") != std::string::npos, "rva rendered as hex");
}

}  // namespace

int main() {
    std::printf("camface host tests\n");
    testParsing();
    testMatching();
    testResolutionRules();
    testGateRule();
    testRegistryReport();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
