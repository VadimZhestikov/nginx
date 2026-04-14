---
marp: true
theme: default
paginate: true
backgroundColor: '#0f1117'
color: '#e8eaf6'
style: |
  section {
    font-family: 'Segoe UI', system-ui, sans-serif;
    font-size: 28px;
  }
  h1 { color: #7ec8e3; font-size: 2.2em; border-bottom: 2px solid #7ec8e3; padding-bottom: 0.2em; }
  h2 { color: #a5d6a7; font-size: 1.6em; }
  h3 { color: #ffcc80; font-size: 1.2em; }
  code { background: #1e2530; color: #a5d6a7; border-radius: 4px; padding: 0.1em 0.3em; }
  pre  { background: #1e2530; border-left: 4px solid #7ec8e3; }
  strong { color: #ffe082; }
  em { color: #80cbc4; }
  blockquote { border-left: 4px solid #7ec8e3; color: #b0bec5; padding-left: 1em; }
  table { font-size: 0.85em; }
  th { background: #1e2d3d; color: #7ec8e3; }
  td { background: #12161e; }
  .columns { display: grid; grid-template-columns: 1fr 1fr; gap: 2em; }
---

<!-- Slide 1 ─────────────────────────────────────────────── -->

# From a Bird's Eye View

## nginx → Fully Programmable System

**JS COM** · **JS Pilgrim**

*The same reliable path browsers walked — now for nginx*

---

> "We built the foundation in 4 weeks, for ~$700.
> The bigger idea took 25 years to prove itself in browsers.
> It works."

---

<!-- Slide 2 ─────────────────────────────────────────────── -->

# Let's open the box

Before the demos, specs, and code —
let me tell you **why this exists**.

Because a very similar story already happened.

And the result **changed everything**.

---

*You may have used it today before breakfast.*

---

<!-- Slide 3 ─────────────────────────────────────────────── -->

# 1995. The World Wide Web.

```html
<html>
  <body>
    <h1>Welcome to our company!</h1>
    <p>Please call us at 555-1234.</p>
  </body>
</html>
```

Beautiful. Simple.

**But completely frozen.**

Every user sees exactly the same page.
Nothing changes without editing the file and reloading.

---

<!-- Slide 4 ─────────────────────────────────────────────── -->

# Then someone added one line

```html
<script>
  document.getElementById('greeting').textContent =
    'Hello, ' + userName + '!';
</script>
```

Small change. **Enormous consequence.**

The page could now **read** its own structure — and **write** it.

That idea had a name:

## Document Object Model (DOM)

*The entire document, exposed as a tree of objects you can touch.*

---

<!-- Slide 5 ─────────────────────────────────────────────── -->

# What DOM unlocked

| Before DOM | After DOM |
|---|---|
| Static HTML file | Live, reactive application |
| Reload for any change | Real-time updates |
| Design tool | Programming platform |
| Web *pages* | Web *apps* |

**The same underlying engine** (browsers).
**The same underlying protocol** (HTTP).

Just one new idea: *expose the internals as objects*.

> Gmail, Google Maps, Netflix streaming UI —
> all running in the same `<html>` that showed company phone numbers in 1995.

---

<!-- Slide 6 ─────────────────────────────────────────────── -->

# nginx in 2024 is 1995 HTML

```nginx
# nginx.conf
http {
    upstream backend {
        server 10.0.0.1 weight=5;
        server 10.0.0.2 weight=3;
    }
    server {
        listen 80;
        location /api { proxy_pass http://backend; }
    }
}
```

Beautiful. Fast. Reliable.

**But completely frozen.**

Change a weight? **Restart nginx.**
Add a server? **Reload config.**
Respond to runtime events? **Not possible.**

---

<!-- Slide 7 ─────────────────────────────────────────────── -->

# The Question

Browsers tried Java, VBScript, Tcl...
**JavaScript survived.**

JavaScript survived because:
- Async by design — *event loop matches how servers work*
- Largest developer ecosystem on Earth
- Standardised, open, embeddable

### So: what if we did to nginx what DOM did to browsers?

*Expose the entire nginx configuration as a JavaScript object tree.*
*Let scripts read it. Write it. React to it.*

---

**We called it the Configuration Object Model — COM.**

*(Yes, deliberately echoing DOM.)*

---

<!-- Slide 8 ─────────────────────────────────────────────── -->

# Layer 1 — Unwrapping the box

## The COM Tree

```javascript
// nginx.conf has an empty http{} block.
// The rest is JavaScript — live, while nginx runs.

nginx.version          // "1.29.7"
nginx.http.servers[0].name          // "api.example.com"
nginx.http.servers[0].locations[0].handler = myFn  // install handler!

// Upstream weights — change while serving traffic:
nginx.http.upstreams['backend'].peers[0].weight = 10;
nginx.http.upstreams['backend'].peers[1].weight = 1;
```

*Every server. Every location. Every upstream peer.*
*Readable and writable. Right now.*

---

<!-- Slide 9 ─────────────────────────────────────────────── -->

# Layer 2 — Request Handlers

```javascript
// Install a content handler for /api/hello — no restart needed:

nginx.http.servers[0]
  .addLocation('/api/hello').handler = function(req) {

    req.respond(200,
      { 'content-type': 'application/json' },
      JSON.stringify({ user: req.headers['x-user'], ts: Date.now() })
    );
};
```

Full access to:
- Request headers, URI, method, body
- Response headers, status code, body
- `req.ctx` — persistent JS object across the entire request lifecycle

---

<!-- Slide 10 ─────────────────────────────────────────────── -->

# Layer 3 — Live Reconfiguration

**Change upstream weights while actively serving traffic**

```javascript
// A background health checker adjusts weights every 5 seconds:
nginx.setTimeout(5000).then(async function tick() {
  const scores = await fetchHealthScores();   // your logic here

  await nginx.suspendAllWorkers();            // pause all workers atomically
  scores.forEach((s, i) => {
    nginx.http.upstreams['backend'].peers[i].weight = s.weight;
  });
  nginx.resumeAllWorkers();                   // resume with new config

  nginx.setTimeout(5000).then(tick);
});
```

**All workers. Atomically. Zero downtime. Zero restart.**

*nginx.conf never changes. The running system does.*

---

<!-- Slide 11 ─────────────────────────────────────────────── -->

# Layer 4 — Workers & Shared State

nginx workers are separate processes — how do they share state?

```javascript
// SharedWorker: one long-lived JS thread visible to ALL nginx workers
new SharedWorker('/js/rate-limiter.js');

// Inside any nginx worker request handler:
var limiter = new SharedWorker('/js/rate-limiter.js');
limiter.port.postMessage({ check: req.headers['x-api-key'] });
limiter.port.onmessage = function(e) {
  if (e.data.allow) { next(); } else { req.respond(429); }
};
```

Also available: **SharedArrayBuffer** with Atomics —
*lock-free shared memory between all nginx processes.*

> Think of SharedWorker as your in-process Redis —
> without the network hop.

---

<!-- Slide 12 ─────────────────────────────────────────────── -->

# Layer 5 — The Full Stack (JS Pilgrim)

Every phase of the nginx request lifecycle is now programmable:

```
Client TCP
  │
  ├─ L4 inbound filters    (raw bytes — protocol detection, tunneling)
  ├─ Access-phase hooks     (auth, rate-limiting, header injection)
  ├─ Pre-content hooks      (Koa-style middleware, composable)
  ├─ Content handler        (your JS or classic nginx modules)
  ├─ Response-header hooks  (add/rewrite response headers)
  ├─ Body / stream filters  (gzip expansion, HTML injection, JSON transform)
  ├─ Upstream filters       (modify what goes to / comes from backend)
  └─ L4 outbound filters    (shape raw output bytes)

Backend TCP
```

**Each layer is optional. Each layer is pure JavaScript.**
Classic nginx modules still work — now as *one layer among many*.

---

<!-- Slide 13 ─────────────────────────────────────────────── -->

# Async all the way down

```javascript
// Body filter: decompress gzip, inject a tracking pixel, recompress
location.addBodyFilter(async function*(chunks, req) {
  const html = await gunzip(Buffer.concat(chunks));

  // Surgery on the live HTML stream:
  const patched = html.toString()
    .replace('</body>', `<img src="/px.gif?id=${req.id}">\n</body>`);

  yield* gzip(Buffer.from(patched));
});
```

- `async function*` — async generator syntax (ES2020)
- `await` — real suspension, nginx event loop keeps running
- No threads blocked. No extra processes. **Zero overhead on other requests.**

---

<!-- Slide 14 ─────────────────────────────────────────────── -->

# The Growing World of Programmable Servers

The developer community has been voting with their tools:

| Technology | What developers love about it |
|---|---|
| **Node.js** | Write your server logic in JavaScript |
| **Deno / Bun** | Modern JS runtimes with HTTP built in |
| **Express / Fastify** | Middleware composition in code, not config |
| **nginx** | Fast, reliable — but you can't *program* it |

**Node.js web server adoption: 4.6% and climbing**
*(W3Techs, 2025 — up from 3.1% the prior year)*

Developers don't just want a fast proxy.
**They want to own the logic layer.**

---

<!-- Slide 15 ─────────────────────────────────────────────── -->

# What JS COM gives us in that world

| Developer need | JS COM answer |
|---|---|
| Routing by runtime feature flag | `req.ctx`, live location install |
| Multi-tenant config (per customer) | Per-request COM snapshot + restore |
| Hot credential rotation | Live upstream mutation, no restart |
| Custom auth / WAF logic | Pre-content hooks with `await` |
| Stateful rate limiting | SharedWorker + SharedArrayBuffer |
| Protocol bridging (WebSocket → gRPC) | L4 generator filters |
| A/B traffic splitting | Dynamic upstream weight adjustment |

**nginx performance. JavaScript flexibility.**

*We don't compete with Node.js. We give nginx users Node.js superpowers.*

---

<!-- Slide 16 ─────────────────────────────────────────────── -->

# Backward Compatible — by design

*This is the DOM lesson applied correctly.*

Modern browsers still render `<html>` from 1995.
DOM *extended* the model. It didn't replace it.

```nginx
# Your existing nginx.conf — unchanged, fully supported:
http {
    server {
        listen 443 ssl;
        location /static { root /var/www; }
        location /api    { proxy_pass http://backend; }
    }
}
```

Add `js_source /etc/nginx/js/init.js;` at the top.
**Everything you had still works.**
JS layers are additive — you opt in per location, per server.

---

<!-- Slide 17 ─────────────────────────────────────────────── -->

# The plugin system

```javascript
// Load a rate-limiter package — hot, while nginx is running:
nginx.use('/plugins/rate-limiter', {
  window: 60,
  limit:  1000,
  key:    req => req.headers['x-api-key']
});

// Load a WAF plugin from a vendor package:
nginx.use('vendor/cloudflare-waf@2.1.3');
```

- Plugins loaded and unloaded **without restart**
- Sandboxed in their own JavaScript scope
- JavaScript module system: `import` / `export`
- Version-pinned, registry-resolvable packages

*This is the npm ecosystem model — for nginx internals.*

---

<!-- Slide 18 ─────────────────────────────────────────────── -->

# Admin Shell — the live REPL

```
$ wscat -c ws://localhost:9999/repl

> nginx.http.upstreams['backend'].peers[0].weight
5
> nginx.http.upstreams['backend'].peers[0].weight = 0
0
> nginx.http.servers[0].locations.map(l => l.path)
[ '/api', '/static', '/health' ]
```

- WebSocket + JSON-RPC 2.0
- Evaluate JavaScript **in any running worker process**
- Cross-worker relay: run on worker 0, 1, 2... or broadcast to all
- Read config, inspect state, mutate live — **in production, no restart**

> Like `kubectl exec` but for nginx internals.

---

<!-- Slide 19 ─────────────────────────────────────────────── -->

# The numbers

### Built by 1 engineer + AI tools

| Metric | Value |
|---|---|
| Total calendar time | ~4 weeks (evenings + weekends) |
| Total budget | **~$700** (AI API costs) |
| Lines of code | ~2 M (C, JavaScript, Perl tests) |
| Test suites | 37 (all passing) |
| P-series feature steps | 19 complete |
| Documentation | 3 doc trees, 40+ files |

### The AI lesson

> Previously owned by big teams and long timelines.
> Now: one engineer, a good metaphor, and AI tools.
> Almost everything can be prototyped without fear — and without meetings.

---

<!-- Slide 20 ─────────────────────────────────────────────── -->

# Coming next — the box goes deeper

### Performance: JavaScript → Native Code

- **QuickJS JIT/AOT**: JavaScript compiled to native binary via GCC
- Up to **20× speedup** over interpreted JS
- Ship binary modules instead of source scripts
- Started as an April Fool's joke — now has real benchmarks

### AI Integration — *"Ludens"*

```javascript
// AI agent with a direct CLI to live nginx workers:
nginx.ai.route(req, {
  model:   'gpt-4o',
  context: { upstream_health, user_tier, geo },
  action:  (decision) => req.proxy(decision.upstream)
});
```

The sharpest tool to operate on an open heart —
direct, privileged, real-time.

---

<!-- Slide 21 ─────────────────────────────────────────────── -->

# The roadmap as natural evolution

```
Static nginx.conf   →   COM tree (read + write)
                    →   Live reconfiguration, no restart
                    →   Workers, SharedWorkers, SharedArrayBuffer
                    →   Full request/response programmability (JS Pilgrim)
                    →   Plugin ecosystem (vendor packages)
                    →   Admin REPL (live introspection)
                    →   JIT/AOT native compilation
                    →   AI-driven routing (Ludens)
                    →   Gateway Fabric integration
                    →   ...
```

Each step is **additive and backward compatible**.

*This is the browser story — but for infrastructure.*

---

<!-- Slide 22 ─────────────────────────────────────────────── -->

# The path to production

This is a proof of concept with **production DNA**:

| Property | Status |
|---|---|
| Existing nginx.conf: backward compatible | ✅ Verified |
| Event loop: non-blocking, zero impact on latency | ✅ Architectural guarantee |
| Memory footprint: QuickJS ~210 KB | ✅ Embedded |
| Test coverage | ✅ 37 suites, all green |
| Documentation | ✅ Three tiers (user, pilgrim, technical) |
| Core hardened and distillable | ✅ Ready for scoping |

### What "going to production" means

1. Define the supported API surface (subset of what's built)
2. Harden the C layer (memory ownership, error paths)
3. Performance benchmark under real traffic
4. Security review (JS sandbox boundaries, plugin isolation)

---

<!-- Slide 23 ─────────────────────────────────────────────── -->

# Lessons learned

### The metaphor is the architecture

A good metaphor drives decisions faster than any design committee.
"DOM for nginx" meant every question had an obvious answer.

### AI changes the economics of exploration

What took a 10-engineer team 2 years now takes 1 engineer 4 weeks.
**This is not a speed improvement. It is a category change.**

Prototype fearlessly. Throw away what doesn't work. Rework without regret.

### The transition is real

```
Machine code → Assembler → Languages (C, Java, ...) → AI
```

At every step, people wanted to look under the hood.
At every step, it mattered less over time.

*Amplify where you're strong. Don't fight the transition.*

---

<!-- Slide 24 ─────────────────────────────────────────────── -->

# One more thing — the deeper insight

> "Optimization makes code more complex."
> nginx.conf was optimized — that's why it's frozen.

The COM layer is **above** that optimization.
JavaScript doesn't replace nginx's C internals.
It *orchestrates* them — the same way JavaScript doesn't replace
the browser's rendering engine.

The internals stay fast.
The surface becomes programmable.

**This is not a tradeoff.
This is what the DOM lesson taught us.**

---

<!-- Slide 25 ─────────────────────────────────────────────── -->

# From a bird's eye view

```
1995   Static HTML
         └─ DOM → JavaScript → The modern web

2024   Static nginx.conf
         └─ COM → JavaScript → The programmable infrastructure layer?
```

The browser path took ~10 years to mature.
The infrastructure version has the advantage of hindsight —
and AI-assisted construction.

**The question is not whether nginx can be fully programmable.**

*It already is.*

**The question is: what do we build on top of it?**

---

<!-- Slide 26 — Final ─────────────────────────────────────── -->

# Thank you

### Resources

| | |
|---|---|
| Developer manual | `js_com_docs/js-dom-manual.adoc` |
| Full-proxy guide | `js_pilgrim_docs/` |
| Technical internals | `js_dom_doc_tech/` |
| Demo applications | `js_com_apps/` · `js_pilgrim_apps/` |

### The metaphor in one sentence

> nginx.conf is 1995 HTML.
> JS COM is the `<script>` tag.
> What you build next is up to you.

---

*Questions?*

---

<!-- Speaker notes follow ──────────────────────────────────── -->

## Speaker Notes

### Slide 1 (Title) — ~1 min
Open with a pause. Let people read the subtitle. Then: "I want to tell you
a story about a pattern that already worked — spectacularly — and how we
applied the same pattern to nginx."

### Slide 2 (Let's open the box) — ~30 sec
Set up the "unwrapping" metaphor early. This is not a spec review. This is
a discovery.

### Slides 3–5 (Browser history) — ~3 min
Go slowly here. The audience is management and PMs — they lived through this.
Invite them to remember. "You used Gmail today. That's running in the same
<html> that showed phone numbers in 1995." Let it land.

### Slide 6 (nginx in 2024) — ~2 min
Show the nginx.conf. Then say: "Every time you need to change a weight, or
rotate a cert, or add a server — what do you do?" Pause. "You restart. Every
time." Let the parallel with static HTML sink in.

### Slide 7 (The Question) — ~1 min
"Browsers tried Java. Tried VBScript. Tried Tcl. One survived. We know which
one." Don't say JavaScript yet — let someone in the audience say it.

### Slides 8–13 (Layers) — ~8 min, ~1.5 min each
This is the "unwrapping" core. Each slide should feel like pulling out a new
toy from the box. Pause between slides. Let people absorb. 

For Slide 8: actually run the REPL live if possible — `nginx.version`,
`nginx.http.servers[0].name`.

For Slide 10 (live reconfig): "Let me say that again. All workers. Atomically.
Zero downtime. Zero restart. The nginx.conf file never changes."

### Slide 14 (Growing world) — ~2 min
"This is not speculation. Node.js web server adoption went from 3.1% to 4.6%
in one year (W3Techs, 2025). Developers don't just want a fast proxy anymore.
They want to own the logic layer. We can give nginx users that — without
giving up what makes nginx nginx."

### Slide 18 (Admin Shell) — ~1.5 min
If you can demo this live, do it. Open a WebSocket REPL, change an upstream
weight, show the curl response change. This is the most concrete "wow" moment.

### Slide 19 (Numbers) — ~1.5 min
"4 weeks. $700. 37 test suites. I want those numbers to sit with you.
This is what AI-assisted engineering looks like today — not at scale, but
in exploration." 

### Slide 20 (Coming next) — ~1.5 min
Keep this fast. The audience doesn't need the details. The message is:
"There's more in the box. We're not done unwrapping."

### Slides 22–23 (Production / Lessons) — ~2 min
Be honest: this is a proof of concept. The core is solid. What "going to
production" means is scoping: pick the API surface to stabilize, harden,
benchmark, and security-review.

On AI: "I'm not saying AI does the work. I'm saying AI changes the economics
of exploration. And that changes what 1 engineer can propose to a company."

### Slide 25 (Bird's eye view) — ~1 min
This is the callback to the opening. Let it land quietly. "The question is
not whether nginx can be fully programmable. It already is."

### Slide 26 (Thank you) — ~1 min
Point to the documentation directories. Invite people to read the developer
manual if they want depth. The one-sentence summary: repeat it slowly.

---

## Total time estimate

| Section | Slides | Time |
|---|---|---|
| Hook + browser history | 1–5 | ~5 min |
| nginx problem statement | 6–7 | ~3 min |
| Unwrapping the layers | 8–13 | ~8 min |
| Market context | 14–15 | ~3 min |
| Backward compat + plugins + REPL | 16–18 | ~4 min |
| Numbers + roadmap | 19–21 | ~3 min |
| Production + lessons + close | 22–26 | ~4 min |
| **Total** | **26** | **~30 min** |
