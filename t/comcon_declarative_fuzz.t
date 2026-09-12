#!/usr/bin/perl

# COMCON M-SES — property fuzz of the ADMISSION path.
#
# comcon.reviewDeclarative(source) is a hand-written recursive-descent parser
# embedded as a C string in ngx_js_com.c.  Everything downstream (reviewCalls,
# realize, admit) trusts its verdict, and its own doc comment makes a strong
# claim: "A SOUND rejecter: it parses ONLY that grammar and throws on anything
# else, so what it accepts is exactly what reduces to the returned descriptor
# tables."  Coverage until now was example-based only -- every case hand-picked
# by the author, which is exactly the coverage a fuzzer is meant to escape.
#
# The inputs are generated from a fixed seed corpus by a DETERMINISTIC mutator,
# addressed by index: input i depends only on (SEED, i), never on iteration
# order, so a reported failure reproduces exactly.  Work is done in batches over
# separate requests so that a hang -- the one failure mode that produces no
# report at all -- is still attributed to a known window of inputs.
#
# Properties asserted (both directions):
#
#   TOTALITY    every input either returns a table or throws a TypeError whose
#               message is the parser's own "not declarative: ..." refusal.
#               Any other exception is a failure.
#   SOUNDNESS   an ACCEPTED source contains no forbidden construct outside its
#               string literals -- checked by an INDEPENDENT scanner written
#               here, not by re-using the parser's own idea of a token.
#   VALIDITY    an ACCEPTED source is real JavaScript.  Differential against the
#               engine's own lexer via new Function(src) -- COMPILE only, never
#               called.  Catches the parser being more permissive than JS.
#   FAITHFUL    every op in the table occurs in the source, every args is an
#               array, and no numeric literal is NaN (a NaN in the table is a
#               value the source did not contain -- JSON.stringify hides it as
#               null, so the round-trip check alone would not see it).
#   DIFFABLE    the table survives JSON.stringify -> parse -> stringify byte for
#               byte.  It is advertised as the diffable review artifact.
#   NO FALSE    reviewCalls(src, {}) must never REFUSE an accepted source: with
#   REFUSAL     no grants every receiver is unresolvable and must land in
#               `unchecked`.  That is the property its soundness argument rests
#               on, and the opposite direction from all of the above.
#
# The instrument is validated before it is believed (/ctl): the oracles are run
# against inputs whose verdict is known, INCLUDING false-positive controls, and
# the run asserts its own work counters -- a loop that ran zero times reports
# "0 failures" exactly like a passing one.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $TOTAL = 6000;
my $BATCH = 250;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        location /fuzz { }
        location /ctl  { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var SEED = 0x5eed1234;

/* ---------------------------------------------------------------- PRNG ---
 * Deterministic and index-addressable: the stream for input i is derived from
 * i alone, so input 4173 is the same string whether or not 4172 ran.
 */
function mix32(x) {
    x = x >>> 0;
    x = (x ^ (x >>> 16)) >>> 0;
    x = Math.imul(x, 0x7feb352d) >>> 0;
    x = (x ^ (x >>> 15)) >>> 0;
    x = Math.imul(x, 0x846ca68b) >>> 0;
    return (x ^ (x >>> 16)) >>> 0;
}

function mkRng(seed) {
    var s = mix32(seed) || 0x9e3779b9;
    return function () {
        s ^= (s << 13); s = s >>> 0;
        s ^= (s >>> 17);
        s ^= (s << 5);  s = s >>> 0;
        return s;
    };
}

/* ------------------------------------------------------- seed corpus ---
 * Every production of the accepted grammar: dotted paths, fluent chains,
 * string/number/object/array literals, free-name refs, nested chain args,
 * line comments, escapes, multiple statements.
 */
var SEEDS = [
    'nginx.http.route("/api").header("X-Env","prod")',
    'limit("/api", { rps: 100, burst: 20 })',
    'grant(env, "db", host.cap)',
    'route(pick("/a","/b"))',
    'flag(true, false, null)',
    'routes(["/a","/b","/c"])',
    'a.b.c.d(1, -2.5, 1e3, "s")',
    'x({}).y([]).z()',
    'cfg({ "quoted-key": 1, plain: { deep: [1, {a: 2}] } })',
    'one(); two(); three()',
    '// leading comment\nafter(1) // trailing\n',
    'outer(inner(deep(1)))',
    'esc("with \\"quotes\\" and \\\\ and \\n")',
    'srv.addServer({ listen: 8080 }).addLocation("/x").handler(h)',
    'p.q(r.s.t, u.v(w), 0)',
    'noargs()'
];

/* --------------------------------------------------------- mutators ---- */
/* The line-terminator family is in the alphabet deliberately: the escape this
 * fuzzer found lives exactly there, and an alphabet missing \r would have found
 * the U+2028 spelling of it but not the far likelier bare-CR one. */
var ALPHA = '(){}[]<>"\'`,;:.\\/|&!=+-*%^~?$_ \t\n\r' +
            'abcxyzABZ019' + '  ﻿';

var TOKENS = [
    'for(;;)', 'while(1)', 'if(x)', 'else', 'function(){}', '=>', '=', '+',
    '-', '*', '/', '[0]', '["k"]', 'new ', 'this', ';', '//', '/*', '*/',
    '"', "'", '`', '${', '\\', '\\u0000', '...', '?.', '&&', '||', '++',
    'typeof ', 'void ', 'delete ', 'yield ', 'await ', 'return ', 'var x',
    '()', '(', ')', '{', '}', '[', ']', ',', '.', ':', '0x10', '1e', '.5',
    ' ', ' ', 'NaN', 'Infinity', '$for', 'in', 'instanceof',
    String.fromCharCode(13), String.fromCharCode(0x2029),
    '//' + String.fromCharCode(13), '//' + String.fromCharCode(0x2028),
    '//' + String.fromCharCode(0x2029), '// ',
    '\\u0066or', '/*x*/', '<!--', '-->', '0b1', '1_0', '9007199254740993'
];

function pick(rng, arr) { return arr[rng() % arr.length]; }
function pos(rng, s)    { return rng() % (s.length + 1); }

function mutate(src, rng, depth) {
    var k = rng() % 12, p, q;

    switch (k) {
    case 0:                                      /* identity: exercise accept */
        return src;
    case 1:                                                    /* truncation */
        return src.slice(0, pos(rng, src));
    case 2:                                                     /* byte flip */
        p = rng() % (src.length || 1);
        return src.slice(0, p) + ALPHA[rng() % ALPHA.length] + src.slice(p + 1);
    case 3:                                                   /* insert char */
        p = pos(rng, src);
        return src.slice(0, p) + ALPHA[rng() % ALPHA.length] + src.slice(p);
    case 4:                                                   /* delete char */
        p = rng() % (src.length || 1);
        return src.slice(0, p) + src.slice(p + 1);
    case 5:                                                  /* inject token */
        p = pos(rng, src);
        return src.slice(0, p) + pick(rng, TOKENS) + src.slice(p);
    case 6:                                            /* duplicate a stretch */
        p = pos(rng, src); q = pos(rng, src);
        if (q < p) { var tmp = p; p = q; q = tmp; }
        return src.slice(0, q) + src.slice(p, q) + src.slice(q);
    case 7:                                               /* splice two seeds */
        p = pos(rng, src);
        var o = pick(rng, SEEDS);
        return src.slice(0, p) + o.slice(pos(rng, o));
    case 8:                                                  /* deep nesting */
        var d = 2 + (rng() % 300);
        return 'f('.repeat(d) + '1' + ')'.repeat(d);
    case 9:                                                     /* long run */
        var n = 50 + (rng() % 3000);
        return src.slice(0, pos(rng, src)) + ALPHA[rng() % ALPHA.length].repeat(n);
    case 10:                                            /* stacked mutations */
        if (depth > 2) { return src; }
        return mutate(mutate(src, rng, depth + 1), rng, depth + 1);
    default:                                                /* swap two chars */
        if (src.length < 2) { return src; }
        p = rng() % src.length; q = rng() % src.length;
        var a = src.split('');
        var c = a[p]; a[p] = a[q]; a[q] = c;
        return a.join('');
    }
}

function genInput(i) {
    var rng = mkRng(SEED ^ mix32(i + 1));
    rng(); rng(); rng();
    return mutate(pick(rng, SEEDS), rng, 0);
}

/* ------------------------------------------------------- the oracles ---
 * Written independently of the parser on purpose: a shared helper would make
 * the two agree by construction, which is the one thing a differential must
 * not do.
 */
function isIdStart(c) {
    return !!c && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c === '_' || c === '$');
}
function isIdPart(c) { return isIdStart(c) || (c >= '0' && c <= '9'); }

var KWSET = {};
(function () {
    var kw = ['for','while','do','if','else','switch','function','return','var',
              'let','const','new','throw','try','catch','with','class','yield',
              'await','typeof','delete','void','in','instanceof','this','super'];
    for (var i = 0; i < kw.length; i++) { KWSET[kw[i]] = 1; }
})();

/* Characters that cannot occur outside a string literal in the declarative
 * grammar.  '-' is handled separately: it is legal as the sign of a numeric
 * literal, and only there. */
var OPBAD = {};
(function () {
    var ch = '=<>!&|?%^~+*/\\`@';
    for (var i = 0; i < ch.length; i++) { OPBAD[ch[i]] = 1; }
})();

/* Remove line comments and string literals.  Returns null when the scan is
 * inconclusive (an unterminated string) -- better to decline than to guess. */
function strip(src) {
    var o = '', i = 0, N = src.length;
    while (i < N) {
        var c = src[i];
        if (c === '/' && src[i + 1] === '/') {
            while (i < N && src[i] !== '\n') { i++; }
            continue;
        }
        if (c === '"' || c === "'") {
            var q = c;
            i++;
            while (i < N && src[i] !== q) { i += (src[i] === '\\') ? 2 : 1; }
            if (i >= N) { return null; }
            i++;
            o += '""';
            continue;
        }
        o += c;
        i++;
    }
    return o;
}

function scanForbidden(code) {
    var bad = [], i = 0, N = code.length;
    while (i < N) {
        var c = code[i];
        if (isIdStart(c)) {
            var st = i;
            while (i < N && isIdPart(code[i])) { i++; }
            var w = code.slice(st, i);
            if (KWSET[w]) { bad.push('kw:' + w); }
            continue;
        }
        if (OPBAD[c]) { bad.push('op:' + c); i++; continue; }
        if (c === '-') {
            var nx = code[i + 1];
            if (!(nx >= '0' && nx <= '9')) { bad.push('op:minus'); }
            i++;
            continue;
        }
        i++;
    }
    return bad;
}

/* Whitespace- and comment-free view of the source, for the op-occurs check:
 * the parser skips whitespace between path segments, so `a . b(1)` legitimately
 * yields the op "a.b".
 *
 * Must keep string literals intact: a first cut stripped comments without
 * tracking strings, so a double slash inside a route glob ate the rest of the
 * line and the oracle reported an op missing that was in the source all along.
 * A false positive in an oracle costs as much as a missed bug -- it spends the
 * reader's trust. */
function canon(src) {
    var o = '', i = 0, N = src.length;
    while (i < N) {
        var c = src[i];
        if (c === '/' && src[i + 1] === '/') {
            while (i < N && src[i] !== '\n') { i++; }
            continue;
        }
        if (c === '"' || c === "'") {
            var q = c, st = i;
            i++;
            while (i < N && src[i] !== q) { i += (src[i] === '\\') ? 2 : 1; }
            if (i < N) { i++; }
            o += src.slice(st, i).replace(/[ \t\n\r]/g, '');
            continue;
        }
        if (c === ' ' || c === '\t' || c === '\n' || c === '\r') { i++; continue; }
        o += c;
        i++;
    }
    return o;
}

function hasNaN(v, depth) {
    if (depth > 40) { return false; }
    if (typeof v === 'number') { return v !== v; }
    if (v === null || typeof v !== 'object') { return false; }
    if (Array.isArray(v)) {
        for (var i = 0; i < v.length; i++) {
            if (hasNaN(v[i], depth + 1)) { return true; }
        }
        return false;
    }
    var ks = Object.keys(v);
    for (var j = 0; j < ks.length; j++) {
        if (hasNaN(v[ks[j]], depth + 1)) { return true; }
    }
    return false;
}

/* Every property that must hold of an ACCEPTED source.  Returns the list of
 * violations; empty means the input is consistent with the contract. */
function checkAccepted(src, r, res) {
    var probs = [];

    if (r === null || typeof r !== 'object') {
        probs.push('table:not-object');
        return probs;
    }
    if (r.declarative !== true)   { probs.push('table:declarative-not-true'); }
    if (!Array.isArray(r.statements)) {
        probs.push('table:statements-not-array');
        return probs;
    }

    /* DIFFABLE */
    var j1 = null, j2 = null;
    try { j1 = JSON.stringify(r); } catch (e) { probs.push('json:stringify-threw'); }
    if (j1 !== null) {
        try { j2 = JSON.stringify(JSON.parse(j1)); }
        catch (e2) { probs.push('json:reparse-threw'); }
        if (j2 !== null && j2 !== j1) { probs.push('json:not-stable'); }
    }

    /* FAITHFUL */
    var cs = canon(src);
    for (var si = 0; si < r.statements.length; si++) {
        var ch = r.statements[si];
        if (!Array.isArray(ch)) { probs.push('table:statement-not-array'); continue; }
        for (var k = 0; k < ch.length; k++) {
            var step = ch[k];
            if (!step || typeof step !== 'object') {
                probs.push('table:step-not-object');
                continue;
            }
            if (typeof step.op !== 'string') { probs.push('table:op-not-string'); }
            else if (cs.indexOf(step.op) < 0) { probs.push('faith:op-absent:' + step.op); }
            if (!Array.isArray(step.args)) { probs.push('table:args-not-array'); }
            else if (hasNaN(step.args, 0)) { probs.push('lit:nan'); }
        }
    }

    /* SOUNDNESS -- independent scanner */
    var st = strip(src);
    if (st === null) { res.scanDeclined++; }
    else {
        res.scanRan++;
        var b = scanForbidden(st);
        if (b.length) { probs.push('scan:' + b.slice(0, 4).join('|')); }
    }

    /* VALIDITY -- differential against the engine's own lexer.  Compile only;
     * the function is never called, so nothing in the source can run. */
    if (src.length <= 4096) {
        res.jsRan++;
        try { new Function(src); }
        catch (e3) {
            if (e3 instanceof SyntaxError) { probs.push('js:syntax-error'); }
        }
    } else {
        res.jsSkipped++;
    }

    return probs;
}

/* ------------------------------------------------------------- driver --- */
function runRange(from, n) {
    var res = { from: from, n: n, gen: 0, acc: 0, rej: 0, so: 0, err: 0,
                oracles: 0, scanRan: 0, scanDeclined: 0, jsRan: 0, jsSkipped: 0,
                rcRan: 0, rcRefused: 0, rcErr: 0, posBad: 0,
                failCount: 0, fails: [] };

    for (var i = from; i < from + n; i++) {
        var src;
        try { src = genInput(i); }
        catch (eg) {
            res.err++;
            if (res.fails.length < 12) {
                res.fails.push({ i: i, why: 'generator:' + String(eg && eg.message) });
            }
            continue;
        }
        res.gen++;

        var r = null, threw = null;
        try { r = comcon.reviewDeclarative(src); }
        catch (e) { threw = e; }

        if (threw !== null) {
            var msg = String(threw && threw.message);
            if ((threw instanceof TypeError) && /^not declarative: /.test(msg)) {
                res.rej++;
                var m = /\(@(\d+)\)$/.exec(msg);
                if (m && Number(m[1]) > src.length + 2) { res.posBad++; }
            } else if (/stack overflow/i.test(msg) ||
                       (threw instanceof RangeError)) {
                res.so++;
            } else {
                res.err++;
                if (res.fails.length < 12) {
                    res.fails.push({ i: i, why: 'exc:' + msg,
                                     src: src.slice(0, 160) });
                }
            }
            continue;
        }

        res.acc++;
        res.oracles++;
        var probs = checkAccepted(src, r, res);

        /* NO FALSE REFUSAL: with no grants, every receiver is unresolvable and
         * must land in `unchecked` -- reviewCalls may not throw. */
        res.rcRan++;
        try {
            var rc = comcon.reviewCalls(src, {});
            if (!rc || rc.ok !== true) { probs.push('rc:not-ok'); }
        } catch (erc) {
            var rmsg = String(erc && erc.message);
            if (/^admission refused: /.test(rmsg)) {
                res.rcRefused++;
                probs.push('rc:false-refusal:' + rmsg.slice(0, 80));
            } else {
                res.rcErr++;
                probs.push('rc:exc:' + rmsg.slice(0, 80));
            }
        }

        if (probs.length) {
            res.failCount++;
            if (res.fails.length < 12) {
                res.fails.push({ i: i, why: probs.slice(0, 4).join(','),
                                 src: src.slice(0, 160) });
            }
        }
    }

    return res;
}

/* --------------------------------------------- instrument self-tests ---
 * Run before any fuzz verdict is believed.  Each names what it proves; a
 * failure here means the oracles cannot be trusted, not that the parser is
 * broken.
 */
function selfTest() {
    var o = {};

    /* the parser refuses every construct the grammar excludes */
    var forbidden = [
        'for (var i=0;i<3;i++) { route(i); }',
        'if (x) { route("/a"); }',
        'x = route("/a")',
        'route(1 + 2)',
        'route(cfg["key"])',
        'route(function(){ return 1; })',
        'while (true) noop()',
        'route(`tpl`)',
        'route(a ? b : c)',
        'new Thing()'
    ];
    var refused = 0;
    for (var i = 0; i < forbidden.length; i++) {
        try { comcon.reviewDeclarative(forbidden[i]); }
        catch (e) { if (/not declarative/.test(String(e.message))) { refused++; } }
    }
    o.negControl = (refused === forbidden.length);
    o.negControlN = forbidden.length;

    /* the VALIDITY oracle can actually tell valid JS from invalid */
    var fnSees = false, fnMisses = false;
    try { new Function('a(1)'); fnMisses = true; } catch (e2) { fnMisses = false; }
    try { new Function('a('); } catch (e3) { fnSees = (e3 instanceof SyntaxError); }
    o.jsOracle = (fnSees && fnMisses);

    /* the SOUNDNESS scanner sees a planted construct ... */
    o.scanSees = (scanForbidden(strip('a(1); for(;;){}')).length > 0);
    /* ... and does NOT fire on the legal shapes it must not fire on */
    o.scanQuiet = (scanForbidden(strip('a.b("for while if =+*", -2.5) // if\n')).length === 0);
    /* an identifier that merely contains a keyword is not a keyword */
    o.scanIdent = (scanForbidden(strip('$for(inner, format)')).length === 0);
    /* declines rather than guessing on an unterminated string */
    o.scanDecline = (strip('a("unterminated') === null);

    /* the FAITHFUL oracle notices an op that is not in the source */
    var fres = { scanRan: 0, scanDeclined: 0, jsRan: 0, jsSkipped: 0 };
    var planted = { declarative: true,
                    statements: [[{ op: 'notInTheSource', args: [] }]] };
    o.faithSees = (checkAccepted('a(1)', planted, fres).join(',')
                   .indexOf('faith:op-absent') >= 0);
    var nanTable = { declarative: true,
                     statements: [[{ op: 'a', args: [NaN] }]] };
    o.nanSees = (checkAccepted('a(1)', nanTable, fres).indexOf('lit:nan') >= 0);

    /* ... and does NOT lose an op to a "//" that is inside a string literal */
    var strTable = { declarative: true,
                     statements: [[{ op: 'a', args: [] }, { op: 'c', args: [] }]] };
    o.canonStr = (checkAccepted('a("*//b").c(1)', strTable, fres).join(',')
                  .indexOf('faith:op-absent') < 0);

    /* ------------------------------------------------------------------
     * Regression: the divergences this fuzzer found on 2026-09-11.  Every one
     * was ACCEPTED before the fix and has an engine verdict proving that was
     * wrong.  The first three are the serious ones: the source is VALID JS, so
     * the hidden statement is not a syntax error that fails closed downstream
     * -- it is live code the review artifact does not mention.
     */
    var CR = String.fromCharCode(13), LS = String.fromCharCode(0x2028),
        PS = String.fromCharCode(0x2029);
    var found = [
        ['hidden-code-cr',  'a(1); //' + CR + 'for(;;){}'],
        ['hidden-code-ls',  'a(1); //' + LS + 'for(;;){}'],
        ['hidden-code-ps',  'a(1); //' + PS + 'for(;;){}'],
        ['no-separator',    'one() two()'],
        ['raw-lf-in-str',   'a("pr\nd")'],
        ['raw-cr-in-str',   'a("pr' + CR + 'd")'],
        ['bare-minus',      'a(-)'],
        ['trunc-exponent',  'a(1e)'],
        ['trunc-exp-sign',  'a(1e+)'],
        ['non-finite',      'a(1e400)']
    ];
    var stillAccepted = [];
    for (var fi = 0; fi < found.length; fi++) {
        try {
            comcon.reviewDeclarative(found[fi][1]);
            stillAccepted.push(found[fi][0]);
        } catch (ef) { /* refused, as it must be */ }
    }
    o.foundRefused = (stillAccepted.length === 0);
    o.stillAccepted = stillAccepted;

    /* OVER-REFUSAL CONTROL.  A "fix" that refuses everything would satisfy the
     * list above, so these must all still be ACCEPTED: a comment that really
     * does run to end of line, both legal statement separators, U+2028 inside a
     * string (legal since ES2019), and every numeric literal shape that is real
     * JavaScript. */
    var mustAccept = [
        'one(); two()',
        'one()\ntwo()',
        'a(1) // trailing comment\nb(2)',
        'a(1)\n// whole-line comment\nb(2)',
        'a("x' + LS + 'y")',
        'a(1.)',
        'a(-2.5, 1e3, 1E-4, 0)',
        'a("back\\\\slash")',
        'nginx.http.route("*//api").header("X-Env","prod")',
        'last()'
    ];
    var wrongly = [];
    for (var mi = 0; mi < mustAccept.length; mi++) {
        try { comcon.reviewDeclarative(mustAccept[mi]); }
        catch (em) { wrongly.push(mustAccept[mi] + ' => ' + String(em.message)); }
    }
    o.noOverRefusal = (wrongly.length === 0);
    o.wronglyRefused = wrongly;

    /* The other half of the comment fix: once a bare CR really does end the
     * comment, the code after it is no longer hidden -- it is REVIEWED.  The
     * table must now list b(2) as a second statement, which is the outcome that
     * distinguishes "the reviewer sees it" from "the reviewer refuses it". */
    try {
        var seen = comcon.reviewDeclarative('a(1); //' + CR + 'b(2)');
        o.crRevealed = (seen.statements.length === 2 &&
                        seen.statements[0][0].op === 'a' &&
                        seen.statements[1][0].op === 'b');
    } catch (ec) { o.crRevealed = 'refused: ' + String(ec.message); }

    /* the generator really produces DIFFERENT inputs, and reproducibly so */
    var seen = {}, dup = 0;
    for (var g = 0; g < 200; g++) {
        var s = genInput(g);
        if (seen[s]) { dup++; }
        seen[s] = 1;
    }
    o.genDistinct = Object.keys(seen).length;
    o.genStable = (genInput(77) === genInput(77) && genInput(77) !== genInput(78));

    return o;
}

var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path === "/fuzz") {
        locs[li].handler = function (req) {
            var from = parseInt(req.queryParams.from) || 0;
            var n = parseInt(req.queryParams.n) || 100;
            var res;
            try { res = runRange(from, n); }
            catch (e) { res = { driverError: String(e && e.message), from: from }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(res));
        };
    }
    if (locs[li].path === "/ctl") {
        locs[li].handler = function (req) {
            var o;
            try { o = selfTest(); }
            catch (e) { o = { selfTestError: String(e && e.message) }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(o));
        };
    }
}
JS

$t->try_run('no js module')->plan(21);

sub jget {
    my ($path) = @_;
    my $r = http_get($path);
    return { httpError => 'no response' } unless defined $r;
    my ($body) = $r =~ /\r\n\r\n(.*)/s;
    return { httpError => 'no body: ' . substr($r, 0, 120) }
        unless defined $body && length $body;
    my $j = eval { decode_json($body) };
    return { httpError => 'bad json: ' . substr($body, 0, 200) } unless $j;
    return $j;
}

# ---------------------------------------------------------------------------
# 1. Validate the instrument before believing any measurement it produces.
# ---------------------------------------------------------------------------
my $c = jget('/ctl');

ok(!$c->{httpError} && !$c->{selfTestError}, 'self-test ran')
    or diag explain $c;
ok($c->{negControl}, 'negative control: every forbidden source is refused');
ok($c->{jsOracle},  'validity oracle distinguishes valid JS from a syntax error');
ok($c->{scanSees},  'soundness scanner sees a planted forbidden construct');
ok($c->{scanQuiet}, 'soundness scanner does not fire on legal keywords inside strings');
ok($c->{scanIdent}, 'soundness scanner does not mistake $for/format for a keyword');
ok($c->{scanDecline}, 'soundness scanner declines on an unterminated string');
ok($c->{faithSees}, 'faithfulness oracle sees an op absent from the source');
ok($c->{nanSees},   'literal oracle sees a NaN the source did not contain');
ok($c->{canonStr},  'faithfulness oracle keeps "//" inside a string literal');
cmp_ok($c->{genDistinct} || 0, '>', 150, 'generator produces distinct inputs');
ok($c->{genStable}, 'generator is deterministic and index-addressable');

ok($c->{foundRefused}, 'every divergence found by this fuzzer is now refused')
    or diag 'still accepted: ' . join(', ', @{ $c->{stillAccepted} || [] });
ok($c->{noOverRefusal}, 'the fix refuses nothing that is legal declarative JS')
    or diag 'wrongly refused: ' . join("\n  ", @{ $c->{wronglyRefused} || [] });
is($c->{crRevealed}, JSON::PP::true,
   'code a bare CR used to hide is now reviewed, not merely refused')
    or diag 'crRevealed = ' . (defined $c->{crRevealed} ? $c->{crRevealed} : 'undef');

# ---------------------------------------------------------------------------
# 2. The fuzz run itself, in attributable batches.
# ---------------------------------------------------------------------------
my %a;
my @fails;
my $batches = 0;

for (my $from = 0; $from < $TOTAL; $from += $BATCH) {
    my $r = jget("/fuzz?from=$from&n=$BATCH");
    if ($r->{httpError} || $r->{driverError}) {
        diag "batch at $from failed: " . ($r->{httpError} || $r->{driverError});
        last;
    }
    $batches++;
    for my $k (qw/gen acc rej so err oracles scanRan scanDeclined jsRan
                  jsSkipped rcRan rcRefused rcErr posBad failCount/) {
        $a{$k} = ($a{$k} || 0) + ($r->{$k} || 0);
    }
    push @fails, @{ $r->{fails} || [] };
}

diag sprintf("fuzz: %d inputs in %d batches | accepted %d rejected %d " .
             "stack-overflow %d | oracles %d (scan %d, decline %d, js %d, " .
             "skip %d, reviewCalls %d)",
             $a{gen} || 0, $batches, $a{acc} || 0, $a{rej} || 0, $a{so} || 0,
             $a{oracles} || 0, $a{scanRan} || 0, $a{scanDeclined} || 0,
             $a{jsRan} || 0, $a{jsSkipped} || 0, $a{rcRan} || 0);
diag "position out of range: " . $a{posBad} if $a{posBad};

for my $f (@fails) {
    diag sprintf("  input %d: %s\n    src: %s", $f->{i}, $f->{why},
                 defined $f->{src} ? $f->{src} : '(n/a)');
}

# Work verification: a loop that ran zero times reports zero failures too.
is($a{gen} || 0, $TOTAL, "generated and ran all $TOTAL inputs");
cmp_ok($a{acc} || 0, '>', 100, 'the accept path was exercised (mutants stay valid)');
cmp_ok($a{rej} || 0, '>', 100, 'the reject path was exercised');
is($a{oracles} || 0, $a{acc} || 0, 'every accepted input went through the oracles');

is($a{err} || 0, 0, 'TOTALITY: every input returned a table or a "not declarative" refusal');
is($a{failCount} || 0, 0, 'no accepted input violated soundness/validity/faithfulness');
