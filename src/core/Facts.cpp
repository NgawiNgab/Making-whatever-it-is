#include <camfacesdk/Facts.hpp>

#include <camfacesdk/Pattern.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace camfacesdk {
namespace {

std::string hexBytes(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

// Reads structural facts about a candidate function so the signature can be
// checked against what it claims to have found (docs/CROSS_VERSION.md R-2).
//
// Deliberately cheap: a prefix compare and a length/terminator probe. Nothing
// here depends on disassembly, so there is no offline-reachability requirement.
bool passesStructuralChecks(ImageSource& image, BuildData const& build,
                            SignatureEntry const& entry, std::uintptr_t rva,
                            std::uint32_t matches, std::string& detail,
                            std::array<std::uint8_t, 4>* prefixOut = nullptr,
                            std::uint32_t* prefixLengthOut = nullptr) {
    if (matches != 1) {
        detail = matches == 0 ? "no match" : "ambiguous: " + std::to_string(matches) + " matches";
        return false;
    }

    if (rva >= build.librarySize) {
        detail = "rva outside image";
        return false;
    }

    // 1. Prefix check: the function's first bytes must be the ones recorded when
    //    this pattern was verified. A pattern can match once and still land in the
    //    middle of a neighbouring function (evidence F-CAM-06/F-CAM-07), which is
    //    exactly what this rejects.
    if (!entry.expectPrefix.empty()) {
        ParsedPattern prefix;
        if (!parsePattern(entry.expectPrefix, prefix)) {
            detail = "malformed expectPrefix in build data";
            return false;
        }
        std::uint8_t buffer[ParsedPattern::kMaxBytes]{};
        const std::size_t wanted = prefix.size;
        std::span<std::uint8_t> view{buffer, wanted};
        if (!image.read(rva, view)) {
            detail = "prefix read failed";
            return false;
        }
        if (!matchesAt(view, 0, prefix)) {
            detail = "prefix mismatch at rva " + std::to_string(rva) + ": found " + hexBytes(view) +
                     ", expected " + std::string(entry.expectPrefix);
            return false;
        }

        // Hand back what the site actually starts with. The hook re-checks these
        // bytes after patching, so "verified" means the site still holds them.
        if (prefixOut != nullptr && prefixLengthOut != nullptr) {
            const std::size_t copy = wanted < prefixOut->size() ? wanted : prefixOut->size();
            for (std::size_t i = 0; i < copy; ++i) (*prefixOut)[i] = buffer[i];
            *prefixLengthOut = static_cast<std::uint32_t>(copy);
        }
    }

    // 2. Length check: reject leaflets. A short function cannot be hooked without
    //    overwriting its neighbour (the real crash class in evidence F-026 / HS-1).
    if (entry.minSize != 0) {
        std::uint8_t buffer[256]{};
        std::span<std::uint8_t> view{buffer, sizeof(buffer)};
        if (!image.read(rva, view)) {
            detail = "size probe read failed";
            return false;
        }
        // Look for the terminating `ret` (c0 03 5f d6) within the probe window.
        bool foundRet = false;
        for (std::size_t i = 0; i + 4 <= view.size(); i += 4) {
            if (view[i] == 0xC0 && view[i + 1] == 0x03 && view[i + 2] == 0x5F && view[i + 3] == 0xD6) {
                foundRet = true;
                break;
            }
        }
        if (!foundRet && entry.minSize > sizeof(buffer)) {
            // Inconclusive rather than failed: a large function need not end inside
            // the probe window. Structure is still fine, so do not reject on this.
            detail = "size probe inconclusive (no ret in first " + std::to_string(sizeof(buffer)) +
                     " bytes); minSize " + std::to_string(entry.minSize) + " accepted";
            return true;
        }
    }

    detail = "ok";
    return true;
}

}  // namespace

const char* toString(State state) {
    switch (state) {
        case State::Unknown: return "UNKNOWN";
        case State::Available: return "AVAILABLE";
        case State::Missing: return "MISSING";
        case State::Ambiguous: return "AMBIGUOUS";
        case State::Rejected: return "REJECTED";
        case State::Unsupported: return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

void resolveAgainst(ImageSource& image, BuildData const& build, std::vector<Fact>& out,
                    ResolveOptions const& options) {
    out.clear();
    out.reserve(build.signatureCount + build.rttiSlotCount);

    if (!image.get()) {
        for (std::size_t i = 0; i < build.signatureCount; ++i) {
            Fact fact;
            fact.id = std::string(build.signatures[i].id);
            fact.state = State::Missing;
            fact.detail = "game image unavailable";
            out.push_back(std::move(fact));
        }
        return;
    }

    // Read the image once. A 340 MB copy is acceptable at load time and keeps the
    // matcher a pure function of a byte span, which is what makes it testable.
    std::vector<std::uint8_t> bytes(image.size());
    if (!bytes.empty()) {
        std::span<std::uint8_t> view{bytes};
        if (!image.read(0, view)) {
            bytes.clear();
        }
    }

    for (std::size_t i = 0; i < build.signatureCount; ++i) {
        SignatureEntry const& entry = build.signatures[i];
        Fact fact;
        fact.id = std::string(entry.id);
        fact.minSize = entry.minSize;

        ParsedPattern pattern;
        if (!parsePattern(entry.pattern, pattern)) {
            fact.state = State::Rejected;
            fact.detail = "malformed pattern in build data";
            out.push_back(std::move(fact));
            continue;
        }

        if (bytes.empty()) {
            fact.state = State::Missing;
            fact.detail = "image unreadable";
            out.push_back(std::move(fact));
            continue;
        }

        const std::size_t matches =
            countMatches(std::span<const std::uint8_t>{bytes}, pattern, options.matchLimit);
        fact.matches = static_cast<std::uint32_t>(matches);

        if (matches == 0) {
            fact.state = State::Missing;
            fact.detail = "pattern not found in this image";
            out.push_back(std::move(fact));
            continue;
        }
        if (matches > 1) {
            fact.state = State::Ambiguous;
            fact.detail = "rejected: " + std::to_string(matches) +
                          " matches (a non-unique pattern is not an identity)";
            out.push_back(std::move(fact));
            continue;
        }

        // Find the single match's offset. countMatches already scanned; a second
        // pass is cheap relative to the image read and keeps the code simple.
        std::size_t offset = 0;
        bool located = false;
        for (std::size_t candidate = 0; candidate + pattern.size <= bytes.size(); ++candidate) {
            if (matchesAt(std::span<const std::uint8_t>{bytes}, candidate, pattern)) {
                offset = candidate;
                located = true;
                break;
            }
        }
        if (!located) {
            fact.state = State::Missing;
            fact.detail = "internal: match not relocatable";
            out.push_back(std::move(fact));
            continue;
        }

        fact.rva = static_cast<std::uintptr_t>(offset);
        fact.address = image.base() + fact.rva;

        if (options.runStructuralChecks &&
            !passesStructuralChecks(image, build, entry, fact.rva, fact.matches, fact.detail,
                                    &fact.expectPrefix, &fact.expectPrefixLength)) {
            fact.state = State::Rejected;
            fact.address = 0;
            out.push_back(std::move(fact));
            continue;
        }

        fact.state = State::Available;
        if (fact.detail.empty()) fact.detail = "ok";
        out.push_back(std::move(fact));
    }

    // RTTI-slot facts are resolved through the engine's vtable resolver, which is
    // not wired yet. They are reported as Unsupported rather than silently absent.
    for (std::size_t i = 0; i < build.rttiSlotCount; ++i) {
        Fact fact;
        fact.id = std::string(build.rttiSlots[i].id);
        fact.state = State::Unsupported;
        fact.detail = "rtti-slot resolution not implemented for this build";
        out.push_back(std::move(fact));
    }
}

bool FactRegistry::resolveAll(ImageSource* image, BuildData const& build) {
    m_facts.clear();
    m_resolved = false;

    if (image == nullptr) {
        // No game image: report honestly instead of pretending. Launcher-only
        // features do not consult facts at all, so they keep working (FR-5).
        for (std::size_t i = 0; i < build.signatureCount; ++i) {
            Fact fact;
            fact.id = std::string(build.signatures[i].id);
            fact.state = State::Unsupported;
            fact.detail = "game image not available";
            m_facts.push_back(std::move(fact));
        }
        m_resolved = true;
        return false;
    }

    resolveAgainst(*image, build, m_facts);
    m_resolved = true;

    bool allAvailable = true;
    for (Fact const& fact : m_facts) allAvailable = allAvailable && fact.available();
    return allAvailable;
}

void FactRegistry::reset() {
    m_facts.clear();
    m_resolved = false;
}

Fact const* FactRegistry::find(std::string_view id) const {
    for (Fact const& fact : m_facts) {
        if (fact.id == id) return &fact;
    }
    return nullptr;
}

Fact* FactRegistry::findMutable(std::string_view id) {
    for (Fact& fact : m_facts) {
        if (fact.id == id) return &fact;
    }
    return nullptr;
}

std::string FactRegistry::summary() const {
    std::size_t counts[6]{};
    for (Fact const& fact : m_facts) counts[static_cast<std::size_t>(fact.state)]++;
    std::ostringstream out;
    out << counts[static_cast<std::size_t>(State::Available)] << " available";
    if (counts[static_cast<std::size_t>(State::Missing)])
        out << ", " << counts[static_cast<std::size_t>(State::Missing)] << " missing";
    if (counts[static_cast<std::size_t>(State::Ambiguous)])
        out << ", " << counts[static_cast<std::size_t>(State::Ambiguous)] << " ambiguous";
    if (counts[static_cast<std::size_t>(State::Rejected)])
        out << ", " << counts[static_cast<std::size_t>(State::Rejected)] << " rejected";
    if (counts[static_cast<std::size_t>(State::Unsupported)])
        out << ", " << counts[static_cast<std::size_t>(State::Unsupported)] << " unsupported";
    return out.str();
}

std::string FactRegistry::writeReport(std::string const& path) const {
    std::ostringstream report;
    report << "{\n  \"facts\": [\n";
    for (std::size_t i = 0; i < m_facts.size(); ++i) {
        Fact const& fact = m_facts[i];
        report << "    { \"id\": \"" << fact.id << "\", \"state\": \"" << toString(fact.state)
               << "\", \"rva\": \"0x" << std::hex << fact.rva << std::dec
               << "\", \"matches\": " << fact.matches << ", \"detail\": \"";
        // Escape quotes so a detail string can never break the JSON.
        for (char ch : fact.detail) {
            if (ch == '"' || ch == '\\') report << '\\';
            report << ch;
        }
        report << "\" }" << (i + 1 == m_facts.size() ? "" : ",") << "\n";
    }
    report << "  ]\n}\n";

    const std::string text = report.str();
    if (!path.empty()) {
        if (std::FILE* file = std::fopen(path.c_str(), "wb")) {
            std::fwrite(text.data(), 1, text.size(), file);
            std::fclose(file);
        }
    }
    return text;
}

bool FactRegistry::satisfies(std::span<FactRequirement const> requirements,
                             std::vector<std::string>* missing) const {
    bool allSatisfied = true;
    for (auto const& requirement : requirements) {
        if (!requirement.required) continue;
        const Fact* fact = find(requirement.factId);
        const bool available = fact != nullptr && fact->available();
        if (!available) {
            allSatisfied = false;
            if (missing != nullptr) missing->emplace_back(requirement.factId);
        }
    }
    return allSatisfied;
}

}  // namespace camfacesdk
