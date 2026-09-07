#!/usr/bin/env bash
# Acceptance test for the mirror ev.* runtime surface (mirror/lib/mirror.js).
# Runs standalone under qjs (the QuickJS REPL) — no nginx build or running
# instance required. Mirrors mirror/transpile/run.sh.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"      # mirror/test
ROOT="$(cd "$DIR/../.." && pwd)"          # nginx repo root
QJS="${QJS:-$ROOT/../quickjs/qjs}"        # sibling quickjs checkout

if [ ! -x "$QJS" ]; then
    echo "qjs not found at $QJS — build it with: make -C ../quickjs qjs" >&2
    echo "(or set QJS=/path/to/qjs)" >&2
    exit 2
fi

# The tests are modules: they std.loadFile() mirror.js / schema.js from the
# paths passed in scriptArgs, so there is a single canonical copy of each.
echo "--- ev.* runtime ---"
"$QJS" "$DIR/ev.test.js" "$DIR/../lib/mirror.js"
echo
echo "--- typed API schema (drift) ---"
"$QJS" "$DIR/schema.test.js" "$DIR/../lib/mirror.js" "$DIR/../lib/schema.js"
