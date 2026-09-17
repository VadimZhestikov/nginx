#!/usr/bin/env bash
# L3 — the policy targets by name but pins by content hash.
PORT=8214; DEMO_NAME="L3 pinned by hash"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. the fragment pin: H(H(source) || schema), computed OUTSIDE the tree =="
SRC=$(body /source)
PIN=$(perl -MDigest::SHA=sha256,sha256_hex -e 'local $/; my $s = <STDIN>; print sha256_hex(sha256($s) . "c2-tenant-env-1")' <<<"$SRC" | tr -d '\n')
# body/<<< add a trailing newline; the host's text has none, so compute on the exact bytes:
PIN=$(printf '%s' "$SRC" | perl -MDigest::SHA=sha256,sha256_hex -e 'local $/; my $s = <STDIN>; print sha256_hex(sha256($s) . "c2-tenant-env-1")')
echo "     source: ${SRC:0:60}…"; echo "     pin:    $PIN"
R=$(body "/pin?identity=$PIN&drift=0"); show "reviewed text, reviewed pin" "$R"
check "the reviewed text is admitted"                 '"admitted":true'     "$R"
check "and runs"                                      '"total":750'         "$R"
D=$(body "/pin?identity=$PIN&drift=1"); show "Saturday's patch (one byte moved), same pin" "$D"
check "refused at admission"                          '"admitted":false'    "$D"
check "the refusal names the pin"                     'identity'            "$D"

echo "== 2. the dependency pin: SHA-256 of the library file =="
LIB="$DEMO_DIR/vendor/magic-utils.js"
SHA=$(sha256sum "$LIB" | cut -d' ' -f1)
echo "     $SHA  magic-utils.js"
G=$(body "/dep?sha256=$SHA&path=$LIB"); show "the reviewed library, its hash" "$G"
check "the pinned library is admitted"                '"admitted":true'     "$G"
check "and bound as a closure parameter (lib.slug, lib.clamp)" '"result":"1.4.2 hello-world 2"' "$G"

sed 's/"1.4.2"/"1.4.3"/' "$LIB" > "$DEMO_DIR/vendor/magic-utils-patched.js"
P=$(body "/dep?sha256=$SHA&path=$DEMO_DIR/vendor/magic-utils-patched.js"); show "a 'patch' to the library, the reviewed hash" "$P"
check "the changed file is refused"                   '"admitted":false'    "$P"
check "the refusal says why"                          'hash mismatch'       "$P"
rm -f "$DEMO_DIR/vendor/magic-utils-patched.js"

demo_end
