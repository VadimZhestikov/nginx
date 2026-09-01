// COMCON onboarding generator — a LIBRARY PROGRAM, not an engine builtin.
//
// It turns a learning record (nginx.tenantLearning()) into a paste-ready grant
// stub: for each host-surface path the tenant reached for, a recommendation
// (grant a narrowed facet / refuse / review), the wanted-vs-granted delta, and
// the js_tenant_mode enforce; next step. This is plain host JS — the observe->
// onboard->enforce loop needs no privileged tooling, only the learning data.
//
// Reusable: copy generateContract() into a host js_source, or import it.

// Classification of the withheld host surface by top-level name. This is
// policy KNOWLEDGE (from the increment-A reality check), carried in the tool,
// not in the engine.
var COMCON_CLASS = {
    // level-conflating / omnipotent — cannot be safely granted whole.
    createSocket:  "REFUSE",   Worker:    "REFUSE",   SharedWorker: "REFUSE",
    config:        "REFUSE",   use:       "REFUSE",   install:      "REFUSE",
    require:       "REFUSE",   repl:      "REFUSE",   std:          "REFUSE",
    os:            "REFUSE",   broadcast: "REFUSE",
    // powerful but narrowable — grant a mediated facet, not the raw name.
    nginx:         "REVIEW",   fetch:     "REVIEW"
};

function classify(path) {
    var top = String(path).split(/[.(]/)[0];
    return COMCON_CLASS[top] || "REVIEW";
}

function generateContract(learning) {
    var wants  = (learning && learning.wants)  || [];
    var grants = (learning && learning.grants) || [];
    var lines  = [];
    var n = { REFUSE: 0, REVIEW: 0 };

    lines.push("# COMCON tenant contract (generated from learning mode)");
    lines.push("# mode observed: " + (learning ? learning.mode : "?"));
    lines.push("# granted now:   " + (grants.length ? grants.join(", ") : "(none)"));
    lines.push("#");

    if (!wants.length) {
        lines.push("# The tenant reached for NO withheld host surface.");
        lines.push("# It is ready for: js_tenant_mode enforce;");
        return lines.join("\n") + "\n";
    }

    lines.push("# The tenant reached for the following host surface:");
    for (var i = 0; i < wants.length; i++) {
        var w = wants[i];
        var verdict = classify(w.path);
        n[verdict] = (n[verdict] || 0) + 1;
        var note = verdict === "REFUSE"
            ? "omnipotent — do NOT grant; the fragment must not need this"
            : "narrow to a mediated facet, then grant that facet by name";
        lines.push("#   [" + verdict + "] " + w.path +
                   "  (x" + w.hits + ") — " + note);
    }

    lines.push("#");
    lines.push("# Summary: " + (n.REFUSE || 0) + " to refuse, " +
               (n.REVIEW || 0) + " to review/narrow.");
    lines.push("# When the environment is settled, lock it down:");
    lines.push("js_tenant_mode enforce;");

    return lines.join("\n") + "\n";
}

export { generateContract, classify };
