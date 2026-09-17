#!/usr/bin/env bash
# A2 — the SDK's appetite is measured, not asserted; nothing is run.
PORT=8217; DEMO_NAME="A2 five-minute vendor evaluation"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

E=$(body /evaluate); show "GET /evaluate" "$E"
echo "== the verdict a procurement reviewer reads =="
check "requests five authorities; the brochure declared one" '"verdict": "requests 5 authorities (Date, buildUrl, createSocket, fetch, nginx); declared 1 of 5"' "$E"
check "undocumented: everything but fetch"               '"undocumented": [' "$E"
check "fetch: two call sites with lines"                 '"name": "fetch",'  "$E"
check "JSON is an intrinsic, not authority"              '"name": "JSON",'   "$E"
check "no dynamic code"                                  '"dynamicCode": false' "$E"
check "the imports line a contract would need"           '"imports": [' "$E"

P=$(body /paste); show "GET /paste" "$P"
check "a pasted source is evaluated the same way"        '"clean": "requests 0 authorities ()"' "$P"
check "text after the function expression is refused, not run" '"trailing": "refused:' "$P"
check "dynamic code is named in the verdict"             'uses dynamic code, refused at admission' "$P"

demo_end
