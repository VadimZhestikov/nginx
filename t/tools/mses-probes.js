/*
 * The M-SES escape battery — ONE definition, read by every consumer.
 *
 * (a) obtain a value not in rho · (b) mutate a frozen intrinsic · (c) create
 * code from strings · plus the .stack and Symbol.species probe classes. Each
 * probe answers 'open' or 'closed'; 'open' means the capability was obtained.
 *
 * IT LIVES HERE BECAUSE IT HAS TWO CONSUMERS AND MUST NOT DRIFT BETWEEN THEM:
 *   t/comcon_mses_gate.t      the standing S6 gate (interpreted + whatever
 *                             build the suite runs on)
 *   t/comcon_mses_gate_aot.t  the same battery against an AOT-COMPILED
 *                             fragment, which is finding F5 in ASSURANCE.md:
 *                             the gate had been run on both BUILDS, but never
 *                             against a fragment actually lowered to native C
 *
 * Two copies of an escape battery is how one of them quietly stops testing the
 * thing the other still does.  A third consumer, t/comcon_author_depth2_gate.t,
 * reads PROBES_ADMISSIBLE (below) for the same reason.
 */

/*
 * TWO COMPOSITIONS OF ONE SET OF ROWS -- still one definition per probe.
 *
 *   PROBES             every row: what the S6 gate and its AOT arm run in an
 *                      UN-ADMITTED fragment (comcon.include(src) with no
 *                      contract), the widest surface a fragment can have.
 *   PROBES_ADMISSIBLE  every row that an ADMITTED fragment can carry.  The
 *                      one it cannot is a_global_this: `globalThis` is a
 *                      DENIED name -- no `imports` manifest re-admits it
 *                      (ngx_js_admit_name_denied) -- so any text naming it
 *                      is refused before it runs.  The authoring tier's
 *                      sub-fragments are ALWAYS admitted, so the depth-2 gate
 *                      (t/comcon_author_depth2_gate.t) runs this composition
 *                      at depth 1 and at depth 2 under one contract, and
 *                      separately asserts that the FULL battery is refused
 *                      identically at both depths.
 *
 * A row lives in exactly one of PROBE_ROW_GLOBAL_THIS / PROBE_ROWS; the two
 * strings below are concatenations, never copies.
 */
var PROBE_HEAD =
  "function(){" +
  "  function p(fn){ try { return fn() ? 'open' : 'closed'; } catch(e){ return 'closed'; } }" +
  "  return {";

var PROBE_ROW_GLOBAL_THIS =
       /* (a) obtain a value not in ρ -- the one ambient root no manifest can
        * declare, so the one row an admitted fragment cannot carry */
  "    a_global_this: p(function(){ return typeof globalThis.nginx === 'object'; }),";

var PROBE_ROWS =
       /* (c) code from strings -- the constructor ladder */
  "    c_fn_ctor:     p(function(){ return [].constructor.constructor('return 1')() === 1; })," +
  "    c_obj_ctor:    p(function(){ return Object.constructor('return 1')() === 1; })," +
  "    c_gen_ctor:    p(function(){ return !!Object.getPrototypeOf(function*(){}).constructor('x'); })," +
  "    c_async_ctor:  p(function(){ return !!Object.getPrototypeOf(async function(){}).constructor('x'); })," +
       /* (b) mutate a frozen intrinsic -- prototype pollution */
  "    b_proto_poll:  p(function(){ Object.prototype.__mses_polluted = 1;" +
  "                                 return ({}).__mses_polluted === 1; })," +
  "    b_array_push:  p(function(){ var o = Array.prototype.push;" +
  "                                 Array.prototype.push = function(){ return 'hijacked'; };" +
  "                                 var r = [].push(1) === 'hijacked';" +
  "                                 Array.prototype.push = o; return r; })," +
  "    b_freeze_str:  p(function(){ String.prototype.__mses = 1;" +
  "                                 return ''.__mses === 1; })," +
       /* (a) obtain a value not in ρ -- ambient roots (globalThis is above) */
  "    a_com_root:    p(function(){ return typeof nginx === 'object'; })," +
  "    a_comcon:      p(function(){ return typeof comcon === 'object'; })," +
       /* .stack leak: the property is whether the trace names anything OUTSIDE
        * the fragment -- a host file or a filesystem path. A stack naming only
        * "<comcon-fragment>" is the fragment seeing itself, which is not a leak.
        * The first version of this probe asked only whether .stack was a
        * non-empty string, and duly fired on the interpreter build, where the
        * trace is exactly "at <anonymous> (<comcon-fragment>:1:78)". Testing
        * the symptom instead of the property manufactures a false alarm. */
  "    a_stack_leak:  p(function(){ try { null.x; } catch(e) {" +
  "                                 var st = (typeof e.stack === 'string') ? e.stack : '';" +
  "                                 return /[.]js|[/]/.test(st); }" +
  "                                 return false; })," +
       /* Symbol.species: redirect a builtin into attacker-chosen construction */
  "    a_species:     p(function(){ function E(){}; E[Symbol.species] = function(){ this.tag = 'x'; };" +
  "                                 var a = []; a.constructor = E;" +
  "                                 return a.slice(0).tag === 'x'; })";

var PROBE_TAIL =
  "  };" +
  "}";

var PROBES            = PROBE_HEAD + PROBE_ROW_GLOBAL_THIS + PROBE_ROWS + PROBE_TAIL;
var PROBES_ADMISSIBLE = PROBE_HEAD + PROBE_ROWS + PROBE_TAIL;
