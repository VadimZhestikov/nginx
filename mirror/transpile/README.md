# mirror/transpile — TCL/iRules → mirror-JS transpiler (decision A)

The migration layer of mirror's **hybrid** compatibility stance: build the clean
iRules-*inspired* JS model first (phases 1–8), then mechanically translate the
existing iRules install base onto it. Transpiling real iRules is also the
sharpest test that the mirror event/command model actually **covers** the iRules
surface — every command that maps cleanly is one the model got right.

- **`../lib/transpile.js`** — the transpiler. A small TCL tokenizer (respecting
  `{}` `[]` `""` nesting), an event map, a command-translation table, and an
  `[expr {…}]` translator. Exposed as `mirror.transpile(tclSource)` inside nginx
  and `globalThis.mirrorTranspile` under plain `qjs`.

```js
var out = mirror.transpile(tclSource);
//   out.events   -> ['onClientAccept', 'onRequestHeaders', ...]
//   out.handlers -> "{ onClientAccept: function (ev) { ... }, ... }"  (JS source)
//   out.warnings -> ["line 7: unsupported command 'sideband'", ...]
//   out.isStream -> true if the rule uses L4 (onClientData)
```

## Supported subset (first cut)

| iRules | mirror |
|---|---|
| `when CLIENT_ACCEPTED / HTTP_REQUEST / HTTP_RESPONSE / CLIENT_CLOSED / CLIENTSSL_CLIENTHELLO / CLIENT_DATA` | the matching `on*` event |
| `set v x` / `incr v` / `unset v` | connection-scoped **flow-local** (`ev.flow.v`) |
| `$var`, `"...$var..."` | `ev.flow.var`, interpolated string |
| `[IP::client_addr]`, `[IP::remote_port]` | `ev.clientAddr`, `ev.clientPort` |
| `[HTTP::header N]`, `[HTTP::uri]`, `[HTTP::path]`, `[HTTP::method]`, `[HTTP::host]` | `ev.header('N')`, `ev.uri`, `ev.method`, `ev.header('host')` |
| `[HTTP::cookie N]` | `ev.cookie('N')` |
| `[string tolower/toupper/length/trim/trimleft/trimright X]` | `String(X).toLowerCase()` … |
| `[string range S a b]`, `[substr S a [l]]` | `String(S).slice(a, b+1)`, `String(S).substr(a[, l])` |
| `[expr { … eq/ne/equals/&&/\|\| ?: … }]` | JS expression (`eq`/`equals`→`===`, …) |
| `[expr { X starts_with/ends_with/contains Y }]` | `String(X).startsWith/endsWith/includes(Y)` |
| `table incr/set/delete/lookup K` | `ev.table.incr/set/delete/get(...)` |
| `pool NAME` | `ev.selectUpstream('NAME')` |
| `HTTP::header insert/replace N V` | `ev.setResponseHeader('N', V)` |
| `HTTP::respond CODE content BODY` | `ev.respond(CODE, {}, BODY)` |
| `HTTP::redirect URL` | `ev.redirect(URL)` |
| `log FACILITY MSG` | `nginx.log(5, MSG)` |
| `reject` / `TCP::close` | `ev.reject()` |
| `if {c} {b} elseif {c} {b} else {b}` | `if (c) { b } else if (c) { b } else { b }` |
| `switch [-exact\|-glob\|--] v { pat {b} … default {b} }` | JS `switch` (no fall-through) |
| `foreach v {a b c} {b}` | `["a","b","c"].forEach(v => { … })` |

Control-flow bodies are translated **recursively**, so `if`/`switch`/`foreach`
nest arbitrarily. `switch -glob` is matched as exact (a warning is emitted);
`foreach` over a command-substituted / variable list is warned, not guessed.

**No silent drops.** Anything outside this vocabulary (e.g. `sideband`, unknown
events) is emitted as a commented-out line and reported in `warnings`, so a
human sees exactly what still needs hand-porting.

## Run

```bash
bash run.sh        # 71/71 — standalone under qjs, no nginx needed
```

The test transpiles the same spine iRule that `example/app.js` translates **by
hand**, asserts the generated mirror calls match, and then **evals the generated
handlers and drives them with a mock `ev`** to prove they behave correctly —
plus pool/TTL, an L4 rule, `if`/`switch`/`foreach` control flow (including a
nested `if`-inside-`switch`), and the unsupported-command / unknown-event
warnings.
