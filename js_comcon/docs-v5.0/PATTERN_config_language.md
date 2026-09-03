# PATTERN — building a config language in userland (not a platform increment)

**Status:** PATTERN note (2026-09-03). This replaces the earlier "increment E / M-DSL"
scoping: on review it is **not platform work**. A config-DSL is expressible entirely with
shipped primitives, so it is **up to operators/devs** — the platform ships nothing new for it. The
single genuine platform hook (sound declarative-profile review) is folded into **increment D (D5b)**,
where the CST front-end already lives.

## The principle

The reduction principle (FOUNDATION §8 / SEMANTICS §4.4): there is **no second language** — a config
language is a restricted subset of policy-JS × a restricted capability vocabulary. So "define a
language" is not a kernel operator to build; it is a few lines an operator writes, composing
`env` / `grant` / `quote` / `realize` + the COM setters. Adding a `defineLanguage` operator would
duplicate `env`+`grant`+`realize` — the same reason `includeAt` and `serve` were retired.

## The pattern (all userland)

**1. A language = grant a vocabulary, realize sentences.**
```js
function myConfigLang(sentence) {
  const e = comcon.env();
  comcon.grant(e, "route",  routeCap);      // the vocabulary = the granted names
  comcon.grant(e, "header", headerCap);
  return comcon.realize(comcon.quote(sentence),
                        { imports: ["route", "header"] }, e);  // least-authority
}
```
That *is* `defineLanguage`. The vocabulary is the granted env; an undeclared name cannot be
referenced (admission: free-names ⊆ imports).

**2. External / untrusted config = a cap-free quotation, realized under least-authority.**
```js
const text = fetchConfig(url);                 // via a granted fetch/socket cap
myConfigLang(text);                            // realize under the bounded vocabulary
```
This is SHOWCASE §47 ("propose the config you can't apply") — no new ingestion primitive.

**3. Untrusted authoring without granting COM authority = propose-don't-hold.**
The confined sentence never *calls* the COM. It **returns a cap-free description**; a trusted host
realizer applies the parts it deems safe:
```js
const proposal = fragment(input);    // e.g. { addLocations:[…], headers:[…] } (JSON, cap-free)
applyConfig(proposal);               // trusted host JS: validate + call COM ops (class-checked)
```
The confined side holds no COM authority — it only describes. This is strictly safer than granting
imperative COM caps into the compartment, and it needs no realm-crossing plumbing: `include`/
`realize` already return JSON, and the host apply loop is ordinary trusted js_source.

**4. Structural (class-3) config = the init phase.** Author it in `js_preprocess`/`js_init` at
`init_conf` (before workers), where nginx still wires structure. Ordinary operator JS — no new
mechanism. (Live COM only reaches class-1/2; the class-3 wall is nginx's.)

## The two constraints operators must respect

- **A directive is a parse-time *action*, not a field** — a vocabulary op (or a host apply loop)
  must call the COM setter that actually wires the runtime structure, honoring its **safety class**.
  Never poke stored fields (they don't take effect). Coverage is demand-driven + classified.
- **The host apply loop is the trust boundary** for untrusted proposals — it validates the proposal
  (allowed ops only, class-checked) before touching the COM. Soundly *reviewing/diffing* that
  proposal is where the platform helps (below).

## The one genuine platform hook → D5b

Making a proposal's review **sound** — proving a sentence stays in a declarative subset (no loops /
dynamic / computed access) and normalizing it to diffable **descriptor tables** — needs a static
syntax-profile checker over the CST, which cannot be written in userland. That is the **`syntax_allowed`
profile + descriptor-table normal form**, and it belongs to **increment D5b** (the CST front-end),
already tracked in `INCREMENT_D.md`. When D5b lands, operators' config languages get sound
declarative review for free; until then they use runtime validation in the host apply loop.

## Bottom line

Config languages are a **userland pattern**, up to operators/devs — the reduction principle already
handed them `env`/`grant`/`quote`/`realize` + propose-don't-hold. The platform builds nothing new
here; its only contribution is D5b's analyzability, which it owes increment D regardless.
