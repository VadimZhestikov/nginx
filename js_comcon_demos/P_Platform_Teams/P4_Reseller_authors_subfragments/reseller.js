// THE RESELLER — a tenant that admits tenants of its own.
//
// It holds two things the host granted: a socket wrapper `s` (address and
// port visible, nothing else) and `author`, whose whole authority is "run the
// admission pipeline up to N times, as me".  From this side of the membrane
// the pipeline looks exactly as it does to the host: source text in, a
// callable out, JSON across the boundary.  What it cannot do is widen: a
// sub-fragment gets a COPY of the reseller's own wrapper, narrowed further.

export default function (req) {
    var out = { limit: author.subFragments, used0: author.used };

    // Sub-fragment 1: sees the address, not the port (redacted on the way down).
    var where = author.include(
        "function(a){ return { addr: typeof s.address, port: typeof s.port }; }",
        { imports: [], grants: { s: s }, attenuate: { s: { redact: ["port"] } } });
    out.sub1 = where({});
    out.used1 = author.used;

    // Widening is refused: the reseller was never given `fd`, so it cannot pass it on.
    try {
        author.include("function(a){ return typeof s.fd; }",
            { imports: [], grants: { s: s }, attenuate: { s: { allow: ["address", "port", "fd"] } } });
        out.escalate = "admitted";
    } catch (e) { out.escalate = e.code; }

    // A sub-fragment cannot even NAME its parent's authority: `author` is a free name to it.
    try {
        author.include("function(){ return typeof author; }", { imports: [] });
        out.namesParent = "admitted";
    } catch (e) { out.namesParent = e.code; }

    // Sub-fragment 2: plain data in, data out.
    var twice = author.include("function(a){ return a.x * 2; }", { imports: [] });
    out.sub2 = twice({ x: 21 });
    out.used2 = author.used;

    // A third one is over the limit the host set (the count is LIVE: dropping
    // a callable refunds its slot; this reseller keeps both).
    try {
        author.include("function(){ return 1; }", { imports: [] });
        out.third = "admitted";
    } catch (e) { out.third = e.code; }

    return out;
}
