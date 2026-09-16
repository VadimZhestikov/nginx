// THE HOST — admission is a verdict you can run in CI, before anything is deployed.
//
// comcon.admit(fn, contract) returns {certified: true} or
// {certified: false, code, reject} WITHOUT running the function.  The same
// gate runs inside comcon.include(); a refused include throws with the same
// code on e.code.  Codes are stable across releases: pin your CI to codes.

function verdict(fn, contract) { return comcon.admit(fn, contract); }

function tryInclude(src, contract) {
    try { var f = comcon.include(src, contract); return { admitted: true, result: f({ a: 21 }) }; }
    catch (e) { return { admitted: false, code: e.code || null, why: String(e.message || e).replace(/^comcon\.include: /, "") }; }
}

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
var locs = nginx.http.servers[0].locations;

locs.find(function (l) { return l.path === "/admit"; }).handler = function (req) {
    respond(req, {
        // the structural gate: free names, dynamic code, the argument itself
        clean:     verdict(function (q) { return q.a * 2; },          { imports: [] }),
        freeName:  verdict(function (q) { return nginx.version; },    { imports: [] }),
        declared:  verdict(function (q) { return nginx.version; },    { imports: ["nginx"] }),
        dynCode:   verdict(function (q) { return eval("1+1"); },      { imports: [] }),
        notFn:     verdict(42,                                        { imports: [] }),

        // the test phase: the contract's tests run against the COMPILED fragment
        // inside the compartment -- zero blast radius, host authority denied
        testPass:  tryInclude("function(x){ return x.a * 2; }",
                       { imports: [], tests: "function(f){ if (f({a: 21}) !== 42) throw new Error('math'); }" }),
        testFail:  tryInclude("function(x){ return x.a * 2; }",
                       { imports: [], tests: "function(f){ if (f({a: 21}) !== 999) throw new Error('want 999'); }" }),
        testReach: tryInclude("function(x){ return 1; }",
                       { imports: [], tests: "function(f){ return nginx.version; }" }),

        // the request-field check: with checkRequest on, a fragment may read only
        // the fields a Request actually has (the shape is sealed); a typo or an
        // invented field is refused at admission, not discovered on request 41
        fields:    tryInclude("function(req){ return req.uri + ' ' + req.method; }",
                       { imports: [], checkRequest: true }),
        fieldsNo:  tryInclude("function(req){ return req.bogus; }",
                       { imports: [], checkRequest: true }),

        // the whole closed set, for CI to pin
        refusalCodes: comcon.refusalCodes()
    });
};

nginx.log(6, "D1: /admit returns admission verdicts without running anything");
