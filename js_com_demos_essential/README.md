# Essential nginx JS COM demos

Two self-contained, runnable demos that show the most immediately useful
capabilities of the nginx JavaScript COM layer: live vhost management and
live rate-limit tuning.

Each demo is a stand-alone directory with `nginx.conf`, `handler.js`, a
`test.sh` that runs automatically, and a `README.md` with a manual walkthrough.

## Prerequisites

Build nginx with the JS module first (see the top-level `CLAUDE.md`):

```bash
# from the repo root
make -f objs/Makefile objs/nginx
```

Both demos assume the binary is at `../../objs/nginx` relative to the demo
directory (i.e. `<repo>/objs/nginx`).

---

## E1 — Hosting-provider vhost control

**Problem:** You host thousands of customer sites on one nginx instance.
Customers must be enabled on boarding and disabled on suspension instantly —
no config edit, no `nginx -s reload`.

**Solution:** `nginx.http.addServer(hostname)` + `removeServer(hostname)` +
`rebuildVhostDispatch()` manage virtual servers at runtime.

```bash
cd E1_Hosting_provider_vhost_control
bash test.sh
```

Key operations:
- Enable a tenant: `POST /tenants/enable?alice.example.com`
- Disable a tenant: `POST /tenants/disable?alice.example.com`
- List active tenants: `GET /tenants/list`

---

## E2 — DDoS: hot-patch limit_req while under attack

**Problem:** A DDoS is in progress. You need to drop the request rate, shrink
the burst window, and enable nodelay rejection right now — no reload gap, no
config file touch.

**Solution:** `location.limitReq.limits[0].rate`, `.burst`, `.nodelay` are
live read-write properties. Writing them takes effect on the very next request.

```bash
cd E2_DDoS_limit_req_hot_patch
bash test.sh
```

Key operations:
- Activate mitigation: `POST /ddos/tighten` (2 r/s, burst 2, nodelay)
- Restore normal mode: `POST /ddos/relax` (20 r/s, burst 10)
- Inspect live params: `GET /ddos/status`
