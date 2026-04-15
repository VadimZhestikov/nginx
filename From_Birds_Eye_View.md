---
marp: true
theme: default
paginate: true
backgroundColor: '#0f1117'
color: '#e8eaf6'
style: |
  section {
    font-family: 'Segoe UI', system-ui, sans-serif;
    font-size: 19px;
  }
  h1 { color: #7ec8e3; font-size: 2.2em; border-bottom: 2px solid #7ec8e3; padding-bottom: 0.2em; }
  h2 { color: #a5d6a7; font-size: 1.6em; }
  h3 { color: #ffcc80; font-size: 1.2em; }
  code { background: #1e2530; color: #a5d6a7; border-radius: 4px; padding: 0.1em 0.3em; }
  pre  { background: #1e2530; border-left: 4px solid #7ec8e3; }
  .hljs-string { color: #ff9d5c; }
  .hljs-attr   { color: #ff9d5c; }
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

# nginx in 2026 is pre-DOM HTML with limited scripting

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
Respond to runtime events? **Limited.**

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

# Layer 2 — Request Handlers for NEW location

```javascript
// Install a content handler for dynamically created location /api/hello — no restart needed:

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
// /js/hits.js — one thread, outlives all worker restarts
var hits = 0;
onmessage = function() { postMessage({ hits: ++hits }); };

// Any nginx worker — request handler:
var sw = new SharedWorker('/js/hits.js');
sw.port.postMessage({});
sw.port.onmessage = function(e) {
  req.respond(200, {}, 'Total hits: ' + e.data.hits + '\n');
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
| Lines of code | ~60 000 (C, JavaScript, Perl tests) |
| Test suites | ~220 |
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
                    →   AI safe integration (Ludens)
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
| Test coverage | ✅ 220 test suites, all green |
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
The title on screen: nginx, arrow, fully programmable system. ...
Two project names: JS COM, ... and JS Pilgrim. ...
And the tagline: ...
The same reliable path browsers walked — ... now for nginx. ...
That path was proven over twenty-five years. ...
We are about to walk it again — in weeks, not years.

### Slide 2 (Opening Quote)
On screen is a single quote. ... Let me read it. ...
We built the foundation in four weeks, ... for approximately seven hundred dollars. ...
The bigger idea took twenty-five years to prove itself in browsers. ...
It works. ...
That is the entire slide.

### Slide 3 (Let's open the box)
Let us open the box. ...
Before the demos, the specs, the architecture — ...
let me tell you why this exists. ...
Because a very similar story already happened. ...
And the result ... changed everything.

### Slide 4 (Teaser)
You may have used it today ... before breakfast. ...
Think about that. ...
Gmail. ... Google Maps. ... Netflix. ...
All of them, the product of that same story.

### Slide 5 (1995 HTML code)
1995. ... The World Wide Web. ...
On screen is an HTML page from that era. ...
A heading: Welcome to our company. ...
A paragraph: Please call us at 555-1234. ...
That is the entire page. ...
Beautiful. ... Simple. ...
But completely frozen. ...
Every user sees exactly the same thing. ...
Nothing changes ... without editing the file and reloading.

### Slide 6 (Script tag — DOM)
Then someone added ... one line. ...
On screen: a script tag. ...
document dot getElementById — ... textContent equals "Hello, userName." ...
Small change. ... Enormous consequence. ...
The page could now read its own structure — ... and write it. ...
That idea had a name: ...
The Document Object Model. ...
The entire document — ... exposed as a tree of objects you can touch.

### Slide 7 (Before/After DOM table)
Look at what that one idea unlocked. ...
A two-column table on screen: Before DOM, and After DOM. ...
Before: static HTML files. ... After: live, reactive applications. ...
Before: reload for any change. ... After: real-time updates. ...
Before: a design tool. ... After: a full programming platform. ...
Before: web pages. ... After: web apps. ...
The same underlying engine. ... The same underlying protocol. ...
Just one new idea: ... expose the internals as objects. ...
The quote at the bottom: ...
Gmail, Google Maps, Netflix — ... all running in the same HTML ...
that showed company phone numbers ... in 1995.

### Slide 8 (nginx.conf — frozen)
And here we are. ...
On screen: nginx dot conf. ...
An upstream block with two backend servers, weights five and three. ...
A server listening on port 80. ...
A location proxying to the backend. ...
Beautiful. ... Fast. ... Reliable. ...
But completely frozen. ...
Change a weight? ... Restart nginx. ...
Add a server? ... Reload the config file. ...
Respond to a runtime event? ... Limited. ...
nginx dot conf ... is our pre-DOM HTML.

### Slide 9 (The Question — bullets)
The Question. ...
Browsers did not start with JavaScript. ...
They tried Java. ... VBScript. ... TCL. ...
One survived. ...
Why JavaScript? ...
The bullets on screen: ...
Async by design — its event loop matches exactly how servers work. ...
The largest developer ecosystem on Earth. ...
Standardized. ... Open. ... Embeddable. ...
So: what if we did to nginx ... what the DOM did to browsers? ...
Expose the entire nginx configuration as a JavaScript object tree. ...
Let scripts read it. ... Write it. ... React to it. ...

### Slide 10 (COM reveal)
We called it ... the Configuration Object Model. ...
COM. ...
Yes. ... Deliberately echoing ... DOM.

### Slide 11 (Layer 1 — COM Tree code)
Layer one. ...
The COM Tree. ...
On screen: four lines of JavaScript. ...
nginx dot version — the running version string. ...
nginx dot http dot servers, index zero, dot name — the first server's hostname. ...
nginx dot http dot servers, index zero, dot locations, index zero, dot handler — ...
assign a function, ... and it becomes the live content handler. ...
Below: change upstream peer weights ... while nginx actively serves traffic. ...
Every server. ... Every location. ... Every upstream peer. ...
Readable. ... And writable. ... Right now.

### Slide 12 (Layer 2 — Request Handler code)
Layer two. ... Request handlers for a new location. ...
On screen: install a content handler for a dynamically created location, slash api slash hello. ...
No restart. ... No config file change. ...
The function receives req. ...
req dot respond — status 200, content-type application/json, body. ...
JSON dot stringify of the user header and the timestamp. ...
Below the code: what you have access to. ...
Request headers, URI, method, body. ...
Response headers, status code, body. ...
And req dot ctx — a persistent JavaScript object across the entire request lifecycle.

### Slide 13 (Layer 3 — Live Reconfig code)
Layer three. ... Live reconfiguration. ...
The slide heading: change upstream weights while actively serving traffic. ...
On screen: a health-check timer loop. ...
nginx dot setTimeout, five thousand milliseconds. ...
Fetch health scores from your own logic. ...
nginx dot suspendAllWorkers — ... every worker process pauses. ... Atomically. ...
Loop through scores: set each peer weight. ...
nginx dot resumeAllWorkers. ...
Schedule the next check. ...
Read the bold line at the bottom: ...
All workers. ... Atomically. ...
Zero downtime. ... Zero restart. ...
nginx dot conf ... never changes. ... The running system ... does.

### Slide 14 (Layer 4 — SharedWorker code)
Layer four. ... Workers and shared state. ...
nginx workers are separate OS processes. ... How do they share state? ...
On screen: two parts. ...
The SharedWorker script — hits dot js — one thread, one variable: hits. ...
On every message: increment and reply with the new total. ...
Below: any nginx worker request handler. ...
Connect to the same SharedWorker, send an empty message, get the global count back. ...
Respond with: Total hits, the number. ...
That number is the same ... across every nginx worker process. ...
Also available: SharedArrayBuffer with Atomics — lock-free shared memory between all nginx processes. ...
The quote at the bottom: ...
Think of SharedWorker as your in-process Redis — ... without the network hop.

### Slide 15 (Layer 5 — Full Stack pipeline)
Layer five. ... The full stack. ... JS Pilgrim. ...
On screen: a vertical pipeline diagram. ...
At the top: Client TCP. ...
L4 inbound filters — raw bytes, protocol detection, tunneling. ...
Access-phase hooks — auth, rate limiting, header injection. ...
Pre-content hooks — Koa-style middleware, composable. ...
Content handler — your JavaScript, or any classic nginx module. ...
Response-header hooks — add or rewrite headers. ...
Body and stream filters — gzip expansion, HTML injection, JSON transform. ...
Upstream filters — modify traffic to and from the backend. ...
L4 outbound filters — shape raw output bytes. ...
At the bottom: Backend TCP. ...
Each layer is optional. ... Each layer is pure JavaScript. ...
Classic nginx modules still work — now as one layer among many.

### Slide 16 (Async body filter code)
Async ... all the way down. ...
On screen: a body filter — five lines. ...
An async generator function — native ES2020 syntax. ...
Receive the chunks, decompress with gunzip. ...
One replace call on the live HTML string — injects a tracking pixel. ...
Re-compress. ... Yield the patched bytes downstream. ...
The three bullets: ...
Async generator — real suspension with await. ...
The nginx event loop keeps running during any I/O wait. ...
No threads blocked. ... No extra processes. ...
Zero overhead on other requests.

### Slide 17 (Growing World table)
The developer community has been voting ... with their tools. ...
The table on screen: four rows. ...
Node JS: write your server logic in JavaScript. ...
Deno and Bun: modern runtimes with HTTP built in. ...
Express and Fastify: middleware composition in code, ... not config. ...
nginx: fast, reliable — ... but you cannot program it. ...
The statistic below: ...
Node JS web server adoption is four point six percent and climbing — ...
up from three point one percent the prior year. ... W3Techs, 2025. ...
Developers do not just want a fast proxy. ...
They want to own ... the logic layer. ...
We can give that to nginx users.

### Slide 18 (What JS COM gives — table)
What this unlocks, in concrete terms. ...
A seven-row table on screen. ...
Routing by runtime feature flag: req dot ctx, live location install. ...
Multi-tenant config: per-request COM snapshot and restore. ...
Hot credential rotation: live upstream mutation, no restart. ...
Custom auth and WAF: pre-content hooks with await. ...
Stateful rate limiting: SharedWorker plus SharedArrayBuffer. ...
Protocol bridging — WebSocket to gRPC: L4 generator filters. ...
A/B traffic splitting: dynamic upstream weight adjustment. ...
nginx performance. ... JavaScript flexibility. ...
We do not compete with Node JS. ...
We give nginx users ... Node JS superpowers.

### Slide 19 (Backward Compatible — code + js_source)
This is the DOM lesson ... applied correctly. ...
Modern browsers still render HTML from 1995. ...
DOM extended the model. ... It did not replace it. ...
On screen: an existing nginx dot conf — unchanged. ...
SSL server. ... Static files at slash static. ... Proxy at slash API. ...
Everything already working. ...
One new line: js underscore source, path to your init dot js. ...
That is all. ...
Everything you had still works. ...
JavaScript layers are additive — opt in per location, per server. ... Or not at all.

### Slide 20 (Plugin system code)
The plugin system. ...
On screen: two lines of JavaScript. ...
nginx dot use, slash plugins slash rate-limiter — with configuration. ...
Window: sixty seconds. ... Limit: one thousand requests. ... Key: the API key header. ...
Second line: nginx dot use, a vendor package, pinned to version 2.1.3. ...
The bullets below: ...
Plugins loaded and unloaded ... without restart. ...
Sandboxed in their own JavaScript scope. ...
Import and export — JavaScript module system. ...
Version-pinned, registry-resolvable packages. ...
This is the npm ecosystem model — ... for nginx internals.

### Slide 21 (Admin Shell — REPL terminal)
Something remarkable. ...
A live REPL for nginx. ...
On screen: a terminal session. ...
wscat, connecting to WebSocket at localhost 9999 slash repl. ...
First query: nginx dot http dot upstreams, backend, peers zero, dot weight. ...
Answer: 5. ...
Set it to zero: answer: 0. ...
Third query: map all locations to their paths. ...
Answer: slash API, slash static, slash health. ...
The bullets: WebSocket plus JSON-RPC 2.0. ...
Evaluate JavaScript in any running worker process. ...
Cross-worker relay: run on worker zero, one, two — or broadcast to all. ...
Read config, inspect state, mutate live — in production, ... without restarting. ...
Like kubectl exec — ... but for nginx internals.

### Slide 22 (Numbers table + AI quote)
Built by one engineer. ... With AI tools. ...
The metrics table on screen: ...
Total calendar time: about four weeks, evenings and weekends. ...
Total budget: approximately seven hundred dollars in API costs. ...
Lines of code: about sixty thousand — C, JavaScript, Perl tests. ...
Test suites: about two hundred twenty. ... All passing. ...
P-series feature steps: nineteen complete. ...
Documentation: three doc trees, forty-plus files. ...
And the AI lesson quote at the bottom: ...
Previously owned by big teams and long timelines. ...
Now: one engineer, a good metaphor, and AI tools. ...
Almost everything can be prototyped without fear — ... and without meetings.

### Slide 23 (Coming next — JIT/AOT + Ludens)
The box has more layers. ...
Performance: JavaScript compiled to native binary via GCC. ...
The four bullets: up to twenty times faster than interpreted JavaScript. ...
Ship binary modules instead of source scripts. ...
Started as an April Fool's joke — ... now has real benchmarks. ...
And Ludens — AI integration. ...
On screen: nginx dot ai dot route, the request, the model, the context, and the action. ...
Upstream health. ... User tier. ... Geographic location. ...
The AI makes a routing decision — ... directly inside the live nginx worker. ...
The sharpest tool to operate on an open heart. ...
Direct. ... Privileged. ... Real-time.

### Slide 24 (Roadmap — evolution chain)
The roadmap. ...
On screen: a vertical evolution chain. ...
Starting point: static nginx dot conf. ...
COM tree — read and write. ...
Live reconfiguration, without restart. ...
Workers, SharedWorkers, SharedArrayBuffer. ...
Full request and response programmability — JS Pilgrim. ...
Plugin ecosystem. ...
Admin REPL. ...
Native compilation via JIT and AOT. ...
AI safe integration — Ludens. ...
And the dots continue. ...
Each step is additive ... and backward compatible. ...
This is the browser story — ... for infrastructure.

### Slide 25 (Path to Production — status table)
A proof of concept ... with production DNA. ...
Six rows on screen, all green checkmarks. ...
Existing nginx dot conf: backward compatible — verified. ...
Event loop: non-blocking, zero impact on latency — architectural guarantee. ...
Memory footprint: QuickJS at two hundred ten kilobytes — embedded. ...
Test coverage: two hundred twenty test suites, all green. ...
Documentation: three tiers, every aspect covered. ...
Core hardened and ready for scoping. ...
What going to production means — four steps below: ...
One: define the supported API surface — a subset of what is already built. ...
Two: harden the C layer for memory ownership and error paths. ...
Three: benchmark under real traffic. ...
Four: security-review the JavaScript sandbox boundaries and plugin isolation.

### Slide 26 (Lessons Learned — 3 sections)
Three lessons. ...
First: the metaphor is the architecture. ...
A good metaphor drives decisions faster than any design committee. ...
DOM for nginx meant every question had an obvious answer. ...
Second: AI changes the economics of exploration. ...
What took a ten-engineer team two years ...
now takes one engineer four weeks. ...
Prototype fearlessly. ... Throw away what does not work. ... Rework without regret. ...
Third: the transition is real. ...
On screen: machine code, to assembler, to languages, to AI. ...
At every step, people wanted to look under the hood. ...
At every step, it mattered less. ...
Amplify where you are strong. ... Do not fight the transition.

### Slide 27 (The deeper insight)
One more thing — the deeper insight. ...
A quote on screen: ...
Optimization makes code more complex. ...
nginx dot conf was optimized — that is why it is frozen. ...
The COM layer is above that optimization. ...
JavaScript does not replace nginx's C internals. ...
It orchestrates them — the same way JavaScript does not replace the browser's rendering engine. ...
The internals stay fast. ...
The surface becomes programmable. ...
This is not a tradeoff. ...
This is what the DOM lesson taught us.

### Slide 28 (From a bird's eye view — diagram)
From a bird's eye view. ...
Two rows in an ASCII diagram on screen. ...
Nineteen ninety-five: static HTML. ...
Arrow: DOM arrives. ... JavaScript arrives. ... The modern web arrives. ...
Twenty twenty-four: static nginx dot conf. ...
Arrow: COM arrives. ... JavaScript arrives. ... The programmable infrastructure layer — question mark. ...
The browser path took about ten years. ...
The infrastructure version has the advantage of hindsight — and AI-assisted construction. ...
The bold text at the bottom: ...
The question is not whether nginx can be fully programmable. ...
It already is. ...
The question is — ... what do we build on top of it?

### Slide 29 (Thank You — resources table)
Thank you. ...
The resources table: four rows. ...
Developer manual: js-dom-manual dot adoc. ...
Full-proxy guide: js pilgrim docs. ...
Technical internals: js dom doc tech. ...
Demo applications: js com apps and js pilgrim apps. ...
And the metaphor, one final time: ...
nginx dot conf ... is nineteen ninety-five HTML. ...
JS COM ... is the script tag. ...
What you build next ... is up to you.

### Slide 30 (Questions)
Questions?

---

## Total time estimate

| Section | Slides | Time |
|---|---|---|
| Hook + browser history | 1–7 | ~5 min |
| nginx problem + COM reveal | 8–10 | ~3 min |
| Unwrapping the layers | 11–16 | ~8 min |
| Market context | 17–18 | ~3 min |
| Backward compat + plugins + REPL | 19–21 | ~4 min |
| Numbers + roadmap | 22–24 | ~3 min |
| Production + lessons + close | 25–30 | ~4 min |
| **Total** | **30** | **~30 min** |
