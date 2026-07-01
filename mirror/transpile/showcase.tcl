# showcase.tcl — a realistic, production-shaped iRule used as the mirror
# transpiler's capstone acceptance test. It deliberately exercises the whole
# command surface built across phases 9-15 in ONE rule:
#
#   - the full event spine        (CLIENT_ACCEPTED / HTTP_REQUEST / HTTP_RESPONSE / CLIENT_CLOSED)
#   - data groups                 (class match / class lookup)
#   - string / URI ops            (string tolower, starts_with, contains)
#   - control flow                (if / elseif / else, switch)
#   - early exit                  (HTTP::respond ... ; return)
#   - the `table`                 (cross-worker counters)
#   - pool / LB selection         (pool [class lookup ...])
#   - response manipulation       (HTTP::header insert — security headers)
#   - connection flow-local       ($cip stashed at accept, read at request)
#
# A copy of this rule is transpiled + behaviourally driven in transpile/test.js
# (authoritative), and applied live in example/app.js on /app/ (end-to-end).

when CLIENT_ACCEPTED {
    set cip [IP::client_addr]
    table incr stats:conns
}

when HTTP_REQUEST {
    # 1) hard block: forwarded client IP on the blocklist -> 403, stop here
    set xff [HTTP::header X-Forwarded-For]
    if { [class match $xff equals ip_blocklist] } {
        table incr stats:blocked
        HTTP::respond 403 content "forbidden"
        return
    }

    # 2) crude bot flag by User-Agent substring
    set ua [string tolower [HTTP::header User-Agent]]
    if { [class match $ua contains bad_agents] } {
        set flagged 1
    } else {
        set flagged 0
    }

    # 3) route by path prefix, pool from a data group
    set path [string tolower [HTTP::path]]
    if { $path starts_with "/api/" } {
        set area api
        pool [class lookup api routes]
    } elseif { $path starts_with "/static/" } {
        set area static
        pool [class lookup static routes]
    } else {
        set area web
        pool [class lookup web routes]
    }

    # 4) release channel by header
    switch [HTTP::header X-Channel] {
        beta   { set channel beta }
        canary { set channel canary }
        default { set channel stable }
    }

    table incr stats:req
}

when HTTP_RESPONSE {
    HTTP::header insert X-Area $area
    HTTP::header insert X-Channel $channel
    HTTP::header insert X-Flagged $flagged
    HTTP::header insert X-Frame-Options "DENY"
    HTTP::header insert Strict-Transport-Security "max-age=31536000"
}

when CLIENT_CLOSED {
    table incr stats:closed
}
