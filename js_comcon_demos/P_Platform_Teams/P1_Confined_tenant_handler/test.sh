#!/usr/bin/env bash
# P1 — a confined tenant handler: does real work, holds no authority.
PORT=8200; DEMO_NAME="P1 confined tenant handler"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. the tenant answers the request =="
R=$(get /t -A "platform/1.0" -H "X-Count: 41")
show "GET /t" "$R"
check "200 from the tenant"                       "HTTP/1.1 200"                 "$R"
check "the tenant's own header"                   "X-Tenant: acme"               "$R"
check "request data crossed IN as a copy"         "X-Tenant-Seen-UA: platform/1.0" "$R"
check "the tenant computed on it"                 "count=42"                     "$R"

echo "== 2. the cage, measured from inside the request =="
check "typeof nginx is undefined in the tenant"   "X-Tenant-Caged: yes"          "$R"
check "body agrees"                               "caged=yes"                    "$R"

echo "== 3. a CRLF in a returned header is dropped, not smuggled =="
absent "no X-Evil header reached the client"      "X-Evil:"                      "$R"
absent "the carrier header was dropped too"       "X-Tenant-Try-Inject:"         "$R"

echo "== 4. the host's report =="
D=$(body /denials)
show "GET /denials" "$D"
check "the fleet posture is enforce"              '"mode":"enforce"'             "$D"

demo_end
