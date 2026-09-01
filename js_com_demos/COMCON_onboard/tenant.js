// COMCON onboarding demo — the CONFINED TENANT.
//
// Written (as tenants often are) against a richer host than it was granted. In
// learn mode this is legal: the references are harvested, not fatal, and the
// operator uses the generated /contract stub to decide grant vs refuse before
// switching to enforce.

onRequest(function (req) {
    nginx.http.addServer({});        // wants config mutation  -> REVIEW
    createSocket("127.0.0.1:9000");  // wants a raw socket     -> REFUSE
    fetch("http://origin/api");      // wants network egress   -> REVIEW
    return "handled " + req.uri + "\n";
});

report("COMCON onboard: tenant handler registered");
