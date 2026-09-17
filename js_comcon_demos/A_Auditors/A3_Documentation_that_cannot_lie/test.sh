#!/usr/bin/env bash
# A3 — if it is in the docs it works; if it works it is in the docs.
PORT=8218; DEMO_NAME="A3 documentation that cannot lie"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

D=$(body /docs); show "GET /docs" "$D"
check "the manual names the tenant and its epoch"          'acme -- your available API'                      "$D"
check "the socket: only the fields the mask leaves"         '`s` -- socket: address (read), port (read)'      "$D"
absent "the redacted fd is not documented"                  'fd (read)'                                       "$D"
check "the budget"                                          'budget: 100 per 60 s (key acme:s)'               "$D"
check "the server facet within its glob"                    '`http` -- server facet within `/acme/*`: paths(), route, allowed(path)' "$D"
check "the outbound within its glob, with office hours"     '`out` -- outbound within `https://*.example.com`: request(url)' "$D"
check "office hours are minutes, UTC"                       'open on day mask'                                "$D"
check "the author grant and its slots"                      'may hold 4 sub-fragments at once'                "$D"
check "the bounds, defaults named as defaults"              'allocation: default (16 MB) bytes per invocation' "$D"
check "not pinned, and it says so"                          'identity: not pinned'                            "$D"

M=$(body /model); show "GET /model (the same, as JSON)" "$M"
check "the model is the query the manual renders"           '"kind": "server facet"'                          "$M"

N=$(body /narrow)
check "a new epoch: the manual says so on the next query"   'epoch 1'                                         "$N"

demo_end
