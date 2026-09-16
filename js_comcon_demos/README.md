# js_comcon_demos — COMCON, shown to the people who need it

Twelve self-contained, runnable demos of **COMCON**, the capability-secure
confinement layer that lets nginx run code it does not trust. Each demo has its
own `nginx.conf`, `host.js` (the operator's program), usually a tenant file,
a `test.sh` that starts nginx, asserts and stops, and a `README.md` that says
what you are looking at and why it is shaped that way.

The companion set `../js_com_demos/` shows what the JS host can *do* to nginx
(live surgery, cross-worker state, config generation). This set shows what it
can *safely let someone else do*.

## COMCON in one paragraph

A tenant's JavaScript is compiled at config load inside a **compartment** that
holds nothing. Everything it can touch is a **capability** the host granted,
narrowed by words that only ever narrow (`allow`, `ttl`, `uses`, `cosign`,
…). Admission is a **contract** checked before any request arrives (free
names, dynamic code, tests run in the sandbox). Every gate that fires leaves a
**stable code**. The same fragment runs interpreted or lowered to C with the
same answers and the same gates. No new nginx directive exists: the whole
mechanism is `js_source host.js;` plus `comcon.include()` and the ordinary
`location.handler`.

## Quick start

```bash
# one demo
cd P_Platform_Teams/P1_Confined_tenant_handler && bash test.sh

# all demos, stop on first failure
for t in $(find . -name test.sh | sort); do echo "=== $t ==="; bash "$t" || break; done
```

The binary is `objs/nginx` at the repo root (`auto/configure --add-module=src/js …`;
`t/tools/gate.sh --configure` builds it and the compiled `objs_jit`). Override
with `NGINX=/path/to/nginx`. Ports are 8200–8211 for the servers and 8250–8259
for the socket capabilities the demos mint; nothing else in the tree uses them.

## The audiences, and what each needs to see

| Group | Who | The question they arrive with | Demos |
|---|---|---|---|
| **P** | Platform / SaaS teams | *Can I run a customer's code in my nginx without it becoming my problem?* | P1–P4 |
| **S** | Security / compliance teams | *What exactly can it do, who can make it do it, and how do I read back what it tried?* | S1–S4 |
| **O** | Operators / SREs | *How do I let tenants change config, and what do I look at when I am paged?* | O1–O2 |
| **D** | Developers / architects | *What will refuse my fragment, and what changes when it is compiled?* | D1–D2 |

## Demo index

### P — Platform teams (ports 8200–8203)

| Demo | Port | What you see |
|---|---|---|
| [P1 Confined tenant handler](P_Platform_Teams/P1_Confined_tenant_handler/) | 8200 | A tenant serves real traffic; `typeof nginx` is `undefined` inside; a CRLF in its header is dropped |
| [P2 Observe, then enforce](P_Platform_Teams/P2_Observe_then_enforce/) | 8201 | The same tenant under `onViolation: audit` and `deny` side by side; a greedy tenant harvested in learn mode |
| [P3 Tenant budgets](P_Platform_Teams/P3_Tenant_budgets/) | 8202 | A spinner aborted at 100 ms, a burst refused at 1 MB, a leaker refused with `E_MEM_RETAINED`, the neighbour still served |
| [P4 A reseller authors sub-fragments](P_Platform_Teams/P4_Reseller_authors_subfragments/) | 8203 | A tenant admits two sub-fragments with copies of its own wrapper, narrower; widening is `E_CAP_ESCALATE` |

### S — Security teams (ports 8204–8207)

| Demo | Port | What you see |
|---|---|---|
| [S1 The attenuation vocabulary](S_Security_Teams/S1_Attenuation_vocabulary/) | 8204 | One socket, six membranes, one probe: `allow`, `redact`, `revoke`, `ttl`, `uses`, a stack; the codes each leaves |
| [S2 Two-person rule and protocol](S_Security_Teams/S2_Two_person_rule_and_protocol/) | 8205 | alice is denied, bob completes the quorum; `protocol('address','port*','fd')` enforces order; a one-shot capability |
| [S3 Outbound as a capability](S_Security_Teams/S3_Outbound_as_capability/) | 8206 | `allowHosts('https://*.example.com')` checked in the compartment; the host reads the surviving intent |
| [S4 Sessions: principal to environment](S_Security_Teams/S4_Sessions_principal_to_environment/) | 8207 | `ci@acme` holds the socket, `dev@acme` does not, `greedy@acme` is refused, revoke is a row removal |

### O — Operators (ports 8208–8209)

| Demo | Port | What you see |
|---|---|---|
| [O1 Config proposal: review / apply / rollback](O_Operators/O1_Config_proposal_review_apply_rollback/) | 8208 | A tenant's inert proposal typechecked, diffed, applied with confirmation, rolled back by hash; an ill-typed one leaves nothing behind |
| [O2 Denials dashboard](O_Operators/O2_Denials_dashboard/) | 8209 | The two closed sets of codes, the counters, and `comcon.mode()` turning a denial into a logged allow |

### D — Developers (ports 8210–8211)

| Demo | Port | What you see |
|---|---|---|
| [D1 Admission as CI](D_Developers/D1_Admission_as_CI/) | 8210 | `admit()` verdicts with stable codes, nothing run; the test phase inside the sandbox; request fields as a contract |
| [D2 Compiled tier, same answers](D_Developers/D2_Compiled_tier_same_answers/) | 8211 | The same fragment on `objs` and `objs_jit`: identical answers, the deadline fires on both |

## Five for a talk

1. **P3** — a runaway loop, a burst, a leak: three refusals, one healthy neighbour, no dead worker.
2. **S2** — alice presses the button and is denied; bob presses it and the operation runs.
3. **S1** — one probe, six membranes: the whole vocabulary on one screen.
4. **O1** — a tenant proposes config; `apply` without `confirm` refuses; the ill-typed proposal leaves nothing.
5. **D2** — two binaries, one answer.

## Reading further

The normative record is `../js_comcon/docs-v5.0/`: `OPERATOR_API.md` for
every word used here, `ASSURANCE.md` for the evidence behind each claim, and
`README.md` there for the reading order. Every demo's README names the tests
in `../t/` that pin its behaviour.
