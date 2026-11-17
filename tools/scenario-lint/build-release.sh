#!/usr/bin/env bash
# Build the scenario-lint release asset reproducibly.
#
# The archive holds exactly two files, scenario-lint and LICENSE. Metadata is pinned
# (sorted names, fixed mtime, root ownership), so the same commit always produces the
# same SHA-256 - check it against docs/RELEASES.md before publishing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/dist/scenario-lint.tar.gz}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp "$ROOT/tools/scenario-lint/scenario_lint.py" "$STAGE/scenario-lint"
cp "$ROOT/LICENSE" "$STAGE/LICENSE"
chmod 755 "$STAGE/scenario-lint"
chmod 644 "$STAGE/LICENSE"
mkdir -p "$(dirname "$OUT")"

# No gzip timestamp runs: tar streams to a file, gzip stores no mtime, and every
# header field below is pinned, so mtimes/uids from the builder cannot leak in.
tar --sort=name --mtime='UTC 2026-11-17' --owner=0 --group=0 --numeric-owner \
    -cf - -C "$STAGE" LICENSE scenario-lint | gzip -9n > "$OUT"

echo "RELEASE_ASSET_OK $(sha256sum "$OUT" | cut -d' ' -f1) $(wc -c < "$OUT") bytes -> $OUT"
