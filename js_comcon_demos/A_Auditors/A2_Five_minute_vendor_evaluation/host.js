// THE HOST — the five-minute vendor evaluation: one static read, nothing run.
import sdk from "./vendor-sdk.js";

var REPORT = comcon.std.evaluate(sdk, { declares: ["fetch"] });   // what the brochure says

var locs = nginx.http.servers[0].locations;
locs.find(function (l) { return l.path === "/evaluate"; }).handler = function (req) {
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(REPORT, null, 1));
};
// the same read over a source string pasted from a tarball
locs.find(function (l) { return l.path === "/paste"; }).handler = function (req) {
    var r = {};
    try { r.clean = comcon.std.evaluate("function(req){ return JSON.stringify(req).length; }").verdict; } catch (e) { r.clean = e.message; }
    try { comcon.std.evaluate("function(){ return 1; }; nginx.log(1, 'pwned')"); r.trailing = "ACCEPTED"; }
    catch (e) { r.trailing = "refused: " + e.message; }
    try { r.dynamic = comcon.std.evaluate("function(x){ return eval(x); }").verdict; } catch (e) { r.dynamic = e.message; }
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(r, null, 1));
};
nginx.log(6, "A2: /evaluate (the SDK), /paste (source strings)");
