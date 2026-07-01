#!/usr/bin/env bash
# Acceptance test for the TCL/iRules -> mirror transpiler. Runs standalone under
# qjs (the QuickJS REPL) — no nginx build or running instance required.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"      # mirror/transpile
ROOT="$(cd "$DIR/../.." && pwd)"          # nginx repo root
QJS="${QJS:-$ROOT/../quickjs/qjs}"        # sibling quickjs checkout

if [ ! -x "$QJS" ]; then
    echo "qjs not found at $QJS — build it with: make -C ../quickjs qjs" >&2
    echo "(or set QJS=/path/to/qjs)" >&2
    exit 2
fi

# The transpiler defines globalThis.mirrorTranspile; concatenate it ahead of the
# test so no module/std machinery is needed.
TMP="$(mktemp /tmp/mirror_transpile_test.XXXXXX.js)"
trap 'rm -f "$TMP"' EXIT
cat "$DIR/../lib/transpile.js" "$DIR/test.js" > "$TMP"

"$QJS" "$TMP"
