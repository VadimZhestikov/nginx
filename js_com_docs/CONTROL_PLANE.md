# js_com — remote control plane over the COM (design / scoping)

Status: **scoping / design only.** The coverage assessment's #1 recommendation:
everything the platform solved is east-west and in-process — the COM, the
atomic envelope, snapshots, safety classes — while the missing layer is
north-south: a stable, versioned, authenticated API an external controller can
drive. This doc scopes that layer. The striking finding from grounding: **all
four building blocks already exist in the tree at demo/app grade.** This is an
assembly-and-hardening design, not an invention.

## 0. Definition of done

An external client (curl-grade) can, against a live instance: read state,
submit a desired-state document, get a server-side **plan** (diff), **apply**
with optimistic concurrency (generation CAS), observe **verified convergence**
per worker, and **roll back** — authenticated, class-gated, with `t/`-style
tests. The same desired-state document applies identically via three
transports: REST push, file watch, and HTTP/DB pull.

## 1. Grounding — the four seeds (verified in tree)

| Seed | Where | What it proves |
|---|---|---|
| **REST dispatcher** | `js_com_apps/admin_snapshot_rollback/conf/admin-api.js` — `GET/POST /admin/state, /snapshots, /snapshots/:id, apply, rollback, compact/:id, squash, set, worker` | HTTP control surface as a JS location handler works; 8 `t/js_admin_*.t` files |
| **Desired-state engine** | `conf/admin.js` + `ARCHITECTURE.md` — snapshot = ordered ops; `{prop}` **named descriptors** (resolve by `upstream.name`/`peer.address`/`server.name`/`location.path` at apply time, replacing fragile index paths); baseline reset → apply = *desired state, not delta*; compaction/squash; structural ops (add/removeServer, add/removeLocation, addListener) with the `removedRefs` finalizer discipline | The wire-format problem is already solved, including its hardest lesson (topology-stable addressing) |
| **Safety gateway** | `js_com_apps/safe_config/index.js` — `nginx.safeConfig.apply(path, value, {ack/confirm})`, `plan()` dry-run, gating consulted from `nginx.describe()` classes, SharedWorker fan-out for worker-local writes | Class-gated mutation + dry-run exist; the safety taxonomy is machine-consultable |
| **Convergence stamps** | `js_com_demos/A4_.../A4.1/handler.js` — each worker stamps its applied snapshot id into `nginx.shared` (`sc.cv.{i}`, `sc.cv.desired`); `GET /c/converge` → `{desired, workers[], allConverged}` | The generation/epoch primitive, demo-grade — "the COW trap made visible" |

Known deficiencies the design must fix (all documented in the tree):
last-writer-wins on concurrent writes (checkpoint guide caveat), the 500 ms
suspend-ack timeout with no per-worker failure detail, draft-banner API names,
and no authn/z story.

## 2. Design decisions

**D1 — The wire format is the ops document, not raw COM paths.** The snapshot
format (named descriptors + structural ops + registered-handler references) is
the API's unit of change. Raw `nginx.set(path, …)` index addressing is
accepted for backward compat but deprecated on the wire — `ARCHITECTURE.md`
already calls it "fragile if topology changes."

**D2 — Code never travels over the wire.** The handler registry pattern is a
security decision, not a convenience: ops reference handlers **by name**,
registered at `js_source` time. A control-plane payload can select behavior,
never define it. (When COMCON lands, registered admin plugins become
policy-caged fragments — showcase scenario 28 is exactly this — but the wire
contract does not change.)

**D3 — Generations are first-class and CAS-guarded.** A monotonic config
generation lives in `nginx.shared` (spinlock `incr` = the CAS). Every mutating
request carries `If-Match: <generation>`; mismatch → `409` with current
generation. This fixes the documented last-writer-wins hole and makes two
controllers safe by construction. Every response carries the new generation;
per-worker stamps generalize `sc.cv.*`.

**D4 — Two consistency modes, explicit in the API.**
- `mode=eventual` (default): apply returns `202 {generation}` immediately;
  the client polls `GET /converge?gen=N` (the A4.1 pattern, formalized).
- `mode=atomic`: the `suspendAllWorkers → apply → acks → resume` envelope;
  returns `200` only when all workers ack, `504 {acked:[…], missing:[…]}` on
  timeout. Exposing *which* workers acked is the one small C ask in this
  design (today the promise resolves or times out opaquely).

**D5 — Authorization vocabulary = the safety classes.** Roles map onto the
taxonomy that already exists in C (`ngx_js_com_describe.c`): *viewer* (GET
only), *operator* (`safe` writes), *admin* (`guarded`, must send `ack:true`),
*owner* (`irreversible`, must send `confirm:true`). The API enforces via
`safeConfig` — no second permission model to keep in sync.

**D6 — One document, three transports.** The same desired-state doc arrives
by: (a) REST push; (b) file watch (the feature-coverage doc's "dynamic config
updates from disk" ask — a SharedWorker watches a file, sidestepping the
reload race); (c) HTTP/DB pull by a designated worker (pull = no inbound admin
exposure; GitOps-shaped; needs context-free `nginx.fetch` — the same M.3
primitive the monitor engine and db-connect want). Fleet adapters (xDS, K8s)
are producers of this document, out of process (§4).

## 3. API v1 sketch

```
GET  /api/v1/state                      full COM read model (+ generation)
GET  /api/v1/describe?path=…            safety metadata (proxy to describe())
POST /api/v1/plan                       desired-state doc -> server-side diff
                                        (generalizes safeConfig.plan to a doc)
POST /api/v1/apply    If-Match: <gen>   body: {doc | snapshotId, mode, ack?,
                                        confirm?} -> 202/200/409/504
GET  /api/v1/converge?gen=N             {desired, workers[], allConverged}
GET  /api/v1/generations                audit trail: who applied what, when
POST /api/v1/rollback If-Match: <gen>   to snapshot id or baseline
CRUD /api/v1/snapshots[/:id]            persisted desired-state docs
```

Structured errors everywhere (`{error, class, path, generation}`); OpenAPI
document shipped with the module so controllers can codegen; version pinned in
the path — the reconfig guides' "API names subject to change" banner is
tolerable for the JS API but fatal for a wire protocol, so v1 freezes the
*document format*, not the COM.

## 4. Fleet adapters (out of process, by design)

- **xDS**: an external adapter (or F5 controller component) terminates the
  gRPC/ADS stream and translates CDS/EDS updates into ops docs against
  `/api/v1`. An in-process gRPC server is a non-goal — wrong trust boundary,
  heavy dependency, and the JS layer gains nothing from speaking proto.
- **Kubernetes** (assessment rec #2): a Gateway-API/Ingress controller is just
  another producer — Endpoints churn becomes `{prop}` peer ops, zero reloads.
- **Cross-node atomicity stays the controller's job** (the platform docs
  already say so); the instance's contribution is exactly D3/D4: CAS +
  verified convergence, so a fleet controller can implement two-phase rollout
  ("apply to canary, verify converged+healthy via the monitor engine, then
  fleet") on honest primitives.

## 5. Phased sub-plan

| Sub-phase | Deliverable | C? | Risk |
|---|---|---|---|
| **R.0** | Consolidate the four seeds into one library (`js_com_apps/control_plane/`): admin engine + safeConfig gating + convergence stamps; first-class generation counter (CAS via `shared.incr`) | no | low |
| **R.1** | API v1: versioned routes, doc format frozen (named descriptors only on the wire), `plan` (doc-level diff), `If-Match` CAS → 409, structured errors, OpenAPI | no | med |
| **R.2** | Authn/z: dedicated admin listener (unix socket / 127.0.0.1 / mTLS via the SSL COM), token or client-cert auth, roles = safety classes (D5) | no | med |
| **R.3** | Convergence semantics: formalized eventual mode; atomic mode with **per-worker ack detail** (the one small C ask — surface acked/missing from the suspend envelope) | small | med |
| **R.4** | Pull transports: file-watch (harden the existing pattern) + HTTP/DB pull via context-free `nginx.fetch` (shared with monitors M.3 / db-connect) | shared | med |
| **R.5** | Fleet adapters: reference xDS→v1 adapter (external), K8s controller PoC consuming the same API | no (external) | high |

R.0–R.2 are pure JS over proven parts; nothing blocks on C. R.3's C ask is
small and sharply scoped. R.5 is where rec #2 (Kubernetes) merges into this
track.

## 6. Risks

- **Wire-format freeze vs draft APIs** — the JS COM can keep evolving under
  draft banners *because* the wire format is the ops doc, not the COM; but the
  doc format itself must be versioned from day one (v1 forever parseable).
- **Admin surface = highest-value target.** Mitigations are structural: no
  code on the wire (D2), class-gated roles (D5), dedicated listener, and the
  audit trail endpoint. Still: an `irreversible`-capable credential is
  root-equivalent for the instance — say so in the docs, loudly.
- **Consistency honesty** — eventual mode's divergence window and atomic
  mode's 500 ms bound are inherited from the platform; the API's job is to
  *report* them truthfully (per-worker stamps, 504 detail), not hide them.
- **Two sources of truth** — `nginx.conf` (boot) vs applied docs (runtime).
  The persistent-checkpoint pattern (re-read at `js_preprocess`) is the
  reconciliation story; the API must expose "boot config vs current
  generation" drift rather than pretend it away.

## 7. Non-goals

- In-process gRPC/xDS termination (§4).
- Cross-instance consensus/atomicity (controller's job, on D3/D4 primitives).
- Replacing `nginx.conf` bootstrap or the reload path — reload remains the
  recovery hatch, and the control plane must survive it (generation and
  snapshots persist; workers re-stamp after re-fork).
- Multi-tenant API exposure (that arrives with COMCON-caged admin plugins,
  scenario 28; v1 assumes one trusted operator domain).

---
*Companion to `js_com_apps/admin_snapshot_rollback/ARCHITECTURE.md` (the
engine), `js_com_apps/safe_config/` (the gateway), demo A4.1 (convergence),
`js-reconfig-guide-all-workers.adoc` + `js-reconfig-guide-checkpoint.adoc`
(consistency model), and `mirror/MONITORS.md` (the health signal a fleet
rollout gates on). Design-only until R.0 is opened.*
