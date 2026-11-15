#!/usr/bin/env bash
# Enforces docs/CROSS_VERSION.md D-10: no game-build constant outside data/.
#
# Version-coupled facts (signatures, RVAs, offsets, library sizes) belong in
# data/<version>.cpp and nowhere else. This check exists because the reference's
# equivalent values live inline in source, which is why a game update there means
# a 100-row code diff instead of a data edit (evidence F-008).
#
# Deliberately narrow: it looks for the *shape* of game facts, not for any hex
# number, so ordinary constants (mask widths, sizes) stay legal.
#
# Scope is shipping code (src/, include/) plus data/ purity. tests/ is exempt from
# the signature rule by necessity: a test of the resolver must supply synthetic
# patterns, and those are fake by construction. It is NOT exempt from the RVA rule,
# because a real address in a test would mean someone pasted game data in.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root" || exit 1

failures=0

report() {
    printf '  %s\n' "$1"
    failures=$((failures + 1))
}

echo "checking for game-build literals outside data/ ..."

# 1. Long signature-like token runs: five or more consecutive hex/wildcard bytes.
#    A signature must only ever appear in data/.
hits=$(grep -rInE '([0-9A-Fa-f?]{2} ){5,}[0-9A-Fa-f?]{2}' \
        --include='*.cpp' --include='*.hpp' --exclude-dir=tests src include 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "signature-like byte runs found outside data/:"
    printf '%s\n' "$hits" | sed 's/^/    /'
    report "signature-like literal in source"
fi

# 2. Library identity constants. The size of libminecraftpe.so is build data.
hits=$(grep -rInE '342231392' --include='*.cpp' --include='*.hpp' src include 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "library-size constant outside data/:"
    printf '%s\n' "$hits" | sed 's/^/    /'
    report "library size literal in source"
fi

# 3. RVAs confirmed by evidence must not be hardcoded as fallbacks. This is the
#    subtlest failure mode: a "known good" address that silently outlives a rebuild.
for rva in 0xaf60090 0x9b7f6d0 0xfdb02a8; do
    hits=$(grep -rInF "$rva" --include='*.cpp' --include='*.hpp' src include tests 2>/dev/null || true)
    if [ -n "$hits" ]; then
        echo "verified RVA $rva hardcoded outside data/:"
        printf '%s\n' "$hits" | sed 's/^/    /'
        report "RVA literal in source"
    fi
done

# 4. data/ itself must stay data-only: no feature logic, no hooks.
if grep -rInE 'pl::memory::hook|submitDrawCommands|registerModule' data 2>/dev/null | grep -q .; then
    echo "logic found inside data/:"
    grep -rInE 'pl::memory::hook|submitDrawCommands|registerModule' data | sed 's/^/    /'
    report "logic in data/"
fi

if [ "$failures" -ne 0 ]; then
    echo "FAILED: $failures violation(s); version-specific values belong in data/<version>.cpp"
    exit 1
fi

echo "ok: no game-build literals outside data/"
