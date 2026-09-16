#!/usr/bin/env bash
# S3 — outbound reach as a capability, checked in the compartment.
PORT=8206; DEMO_NAME="S3 outbound as capability"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

O=$(body /outbound)
show "GET /outbound" "$O"

echo "== the glob, checked where the capability is exercised =="
check "an in-glob destination is recorded"            '"inGlob":"recorded"'    "$O"
check "an off-glob destination is denied"             '"offGlob":"denied"'     "$O"
check "the scheme is exact: plain http is denied"     '"plainHttp":"denied"'   "$O"
check "a URL with credentials is refused outright"    '"creds":"refused"'      "$O"
check "the drain half is the host's (reach-gated)"    '"drain":"denied"'       "$O"

echo "== what reached the host =="
check "exactly the surviving intent is in the queue"  'api.example.com/v1/orders' "$O"
absent "the denied destination never reached the host" 'evil.net'               "$O"
check "denials counted: out.host"                     '"out.host":'            "$O"
check "denials counted: out.drain"                    '"out.drain":'           "$O"

demo_end
