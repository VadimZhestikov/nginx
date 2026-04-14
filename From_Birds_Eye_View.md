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

### Slide 1 (Title)
From a bird's eye view. ...
nginx ... to a fully programmable system.
This is the story of two projects: JS COM, and JS Pilgrim. ...
And behind them — a bigger idea.
A pattern that already worked once, ... spectacularly. ...
And that we are now applying ... to nginx.

### Slide 2 (Let's open the box)
Before the demos, the specs, and the architecture — ...
let me tell you ... why this exists. ...
Because a very similar story already happened. ...
And the result ... changed everything. ...
You may have used it today ... before breakfast.

### Slide 3 (1995 HTML)
1995. ... The World Wide Web. ...
You could write a page like this. ...
Beautiful. ... Simple. ...
But completely frozen. ...
Every user sees exactly the same page. ...
Nothing changes ... without editing the file ... and reloading. ...
Every click ... means a full reload of the entire page.

### Slide 4 (Then someone added one line)
Then someone added ... one small thing. ...
The script tag. ...
A small change. ... An enormous consequence. ...
The page could now read its own structure — ... and write it. ...
That idea had a name. ...
The Document Object Model. ...
The entire document, ... exposed as a tree of objects ... you can touch.

### Slide 5 (What DOM unlocked)
Look at what happened. ...
Before the DOM: static HTML files, ... reload for any change, ... a design tool. ...
After the DOM: live, reactive applications. ... Real-time updates. ... A full programming platform. ...
The same underlying engine. ... The same underlying protocol. ...
Just one new idea: ... expose the internals as objects. ...
Gmail, Google Maps, Netflix streaming — ...
all running in the same HTML ... that showed company phone numbers ... in 1995.

### Slide 6 (nginx in 2024 is 1995 HTML)
And here we are. ...
nginx in 2024. ...
Beautiful. ... Fast. ... Reliable. ...
But completely frozen. ...
Change a weight? ... Restart nginx. ...
Add a server? ... Reload config. ...
Respond to runtime events? ... Not possible. ...
Every time you need to change anything — you restart. ...
Every time. ...
nginx dot conf ... is our 1995 HTML.

### Slide 7 (The Question)
Browsers tried Java. ... They tried VBScript. ... They tried TCL. ...
One survived. ...
JavaScript survived — because it is async by design. ...
Its event loop matches exactly ... how servers work. ...
It has the largest developer ecosystem on Earth. ...
It is standardized, ... open, ... and embeddable. ...
So. ...
What if we did to nginx ... what the DOM did to browsers? ...
Expose the entire nginx configuration ... as a JavaScript object tree. ...
We called it ... the Configuration Object Model. ... COM. ...
Yes. ... Deliberately echoing ... DOM.

### Slide 8 (Layer 1 — COM Tree)
Layer one. ... The COM tree. ...
nginx dot version. ... nginx dot http dot servers. ...
nginx dot http dot upstreams dot peers. ...
Every server. ... Every location. ... Every upstream peer. ...
Readable. ... And writable. ...
Right now. ... While nginx is running. ...
No restart. ... No reload. ...
The configuration is no longer a file. ...
It is a live object.

### Slide 9 (Layer 2 — Request Handlers)
Layer two. ... Request handlers. ...
Install a JavaScript function as the content handler ... for any location. ...
No restart needed. ...
You get the full request object — headers, URI, method, body. ...
The full response object — status, headers, body. ...
And req dot ctx — a plain JavaScript object ...
that persists across the entire request lifecycle. ...
Your logic ... belongs to you.

### Slide 10 (Layer 3 — Live Reconfiguration)
Layer three. ... Live reconfiguration. ...
A background health checker ... adjusts upstream weights ... every five seconds. ...
It suspends all workers — atomically. ...
Updates the weights. ...
Resumes. ...
All workers. ... Atomically. ...
Zero downtime. ... Zero restart. ...
Let me say that again. ...
The nginx dot conf file ... never changes. ...
The running system ... does.

### Slide 11 (Layer 4 — Workers and Shared State)
Layer four. ... Workers and shared state. ...
nginx workers are separate processes — how do they share state? ...
With a SharedWorker. ...
One long-lived JavaScript thread, ...
visible to all nginx workers ... simultaneously. ...
Think of it as your in-process Redis — ... without the network hop. ...
And for fast, lock-free sharing across processes — ...
SharedArrayBuffer ... with Atomics.

### Slide 12 (Layer 5 — Full Stack JS Pilgrim)
Layer five. ... The full stack. ...
JS Pilgrim exposes every phase of the nginx request lifecycle ... as programmable JavaScript. ...
L4 inbound filters on raw bytes. ...
Access-phase hooks for auth and rate limiting. ...
Pre-content hooks — composable middleware. ...
Response header hooks. ...
Body and streaming filters. ...
Upstream filters. ...
L4 outbound filters on raw output bytes. ...
Each layer is optional. ... Each layer is pure JavaScript. ...
Classic nginx modules still work — ...
now as one layer ... among many.

### Slide 13 (Async all the way down)
And it is async ... all the way down. ...
Look at this body filter. ...
It decompresses gzip, ...
injects a tracking pixel into the live HTML stream, ...
and recompresses — ...
using async generator syntax. ...
The await keyword means real suspension. ...
The nginx event loop keeps running. ...
No threads blocked. ... No extra processes. ...
Zero overhead ... on other requests.

### Slide 14 (The Growing World)
The developer community has been voting ... with their tools. ...
Node JS: write your server logic in JavaScript. ...
Deno and Bun: modern JavaScript runtimes with HTTP built in. ...
Express and Fastify: middleware composition in code, ... not config. ...
Node JS web server adoption went from three point one percent ...
to four point six percent ... in a single year. ...
Developers do not just want a fast proxy anymore. ...
They want to own ... the logic layer. ...
We can give nginx users exactly that — ...
without giving up ... what makes nginx ... nginx.

### Slide 15 (What JS COM gives us)
Look at what this unlocks. ...
Routing by runtime feature flag. ...
Multi-tenant configuration per customer. ...
Hot credential rotation ... without restart. ...
Custom authentication and WAF logic ... with async await. ...
Stateful rate limiting with SharedWorker and SharedArrayBuffer. ...
Protocol bridging — WebSocket to gRPC — with L4 generator filters. ...
Live traffic splitting. ...
nginx performance. ... JavaScript flexibility. ...
We do not compete with Node JS. ...
We give nginx users ... Node JS superpowers.

### Slide 16 (Backward Compatible)
And this is the DOM lesson ... applied correctly. ...
Modern browsers still render HTML from 1995. ...
DOM extended the model. ... It did not replace it. ...
Your existing nginx dot conf — unchanged, ... fully supported. ...
Add one line at the top: js underscore source. ...
Everything you had ... still works. ...
JavaScript layers are additive. ...
You opt in per location, per server. ... Or not at all.

### Slide 17 (Plugin system)
And then there is the plugin system. ...
Load a rate-limiter package — hot, ... while nginx is running. ...
Load a WAF plugin from a vendor package. ...
Plugins loaded and unloaded ... without restart. ...
Sandboxed in their own JavaScript scope. ...
Using the npm ecosystem model — ...
for nginx internals.

### Slide 18 (Admin Shell)
And here is something remarkable. ...
A live REPL ... for nginx. ...
WebSocket plus JSON-RPC. ...
Evaluate JavaScript ... in any running worker process. ...
Change an upstream weight — right now. ...
List all locations — right now. ...
Cross-worker relay: run on worker zero, one, two — ... or broadcast to all. ...
Read config, inspect state, mutate live — ...
in production, ... without restarting. ...
Like kubectl exec — ... but for nginx internals.

### Slide 19 (The Numbers)
Built by one engineer. ... With AI tools. ...
Four weeks of evenings and weekends. ...
About seven hundred dollars in API costs. ...
Approximately two million lines of code — C, JavaScript, Perl tests. ...
Thirty-seven test suites. ... All passing. ...
What used to require a ten-engineer team ... and two years — ...
now takes one engineer ... and four weeks. ...
That is not a speed improvement. ...
That is a category change.

### Slide 20 (Coming Next)
And the box has more layers. ...
Performance: JavaScript compiled to native code ...
via QuickJS JIT and AOT compilation. ...
Up to twenty times faster than interpreted JavaScript. ...
Ship binary modules instead of source scripts. ...
And then there is Ludens — AI integration. ...
A direct command-line interface ... to live nginx workers. ...
The sharpest tool to operate on an open heart. ...
Direct. ... Privileged. ... Real-time. ...
AI-driven routing decisions — not as a layer on top of nginx, ...
but as a first-class citizen ... inside it.

### Slide 21 (Roadmap)
The roadmap looks like natural evolution. ...
Static nginx dot conf. ...
Then the COM tree — read and write. ...
Then live reconfiguration without restart. ...
Then Workers, SharedWorkers, SharedArrayBuffer. ...
Then full request and response programmability. ...
Then the plugin ecosystem. ...
Then the admin REPL. ...
Then native compilation via JIT and AOT. ...
Then AI-driven routing. ...
Then Gateway Fabric integration. ...
Each step ... is additive ... and backward compatible. ...
This is the browser story — ... for infrastructure.

### Slide 22 (Path to Production)
This is a proof of concept ... with production DNA. ...
Existing nginx dot conf: backward compatible — verified. ...
Event loop: non-blocking, zero impact on latency — architectural guarantee. ...
Memory footprint: QuickJS two hundred ten kilobytes — embedded. ...
Thirty-seven test suites, all green. ...
Three documentation tiers covering every aspect. ...
What going to production means: ...
define the supported API surface, ...
harden the C layer for memory ownership and error paths, ...
benchmark under real traffic, ...
and security-review the JavaScript sandbox boundaries.

### Slide 23 (Lessons Learned)
Three lessons worth sharing. ...
First: the metaphor is the architecture. ...
A good metaphor drives decisions ... faster than any design committee. ...
DOM for nginx meant every question had an obvious answer. ...
Second: AI changes the economics of exploration. ...
What took a ten-engineer team two years ...
now takes one engineer four weeks. ...
I am not saying AI does the work. ...
I am saying AI changes the economics of exploration. ...
And that changes what one engineer ... can propose to a company. ...
Third: the transition is real. ...
Machine code to assembler, to languages, to AI. ...
At every step, people wanted to look under the hood. ...
At every step ... it mattered less. ...
Amplify where you are strong. ... Do not fight the transition.

### Slide 24 (The Deeper Insight)
One more thing — the deeper insight. ...
Optimization makes code more complex. ...
nginx dot conf was optimized — ... that is why it is frozen. ...
The COM layer is above that optimization. ...
JavaScript does not replace nginx's C internals. ...
It orchestrates them — ...
the same way JavaScript does not replace the browser's rendering engine. ...
The internals stay fast. ...
The surface becomes programmable. ...
This is not a tradeoff. ...
This is what the DOM lesson taught us.

### Slide 25 (From a Bird's Eye View)
From a bird's eye view. ...
Nineteen ninety-five: static HTML. ...
The DOM arrived. ... JavaScript arrived. ... The modern web arrived. ...
Twenty twenty-four: static nginx dot conf. ...
COM arrives. ... JavaScript arrives. ...
The programmable infrastructure layer ... arrives. ...
The browser path took about ten years to mature. ...
The infrastructure version has the advantage of hindsight — ...
and AI-assisted construction. ...
The question is not whether nginx can be fully programmable. ...
It already is. ...
The question is — ...
what do we build on top of it?

### Slide 26 (Thank You)
Thank you. ...
The developer manual, full-proxy guide, technical internals,
and demo applications — all checked in to the repository. ...
One sentence to leave you with. ...
nginx dot conf ... is nineteen ninety-five HTML. ...
JS COM ... is the script tag. ...
What you build next ... is up to you. ...
Questions?

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
