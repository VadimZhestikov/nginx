# 07 — SharedArrayBuffer and Cross-Process Shared Memory

## Background

`SharedArrayBuffer` (SAB) lets multiple JS contexts share a contiguous memory
region.  In a single-process setup (Node.js Worker threads), this is trivial:
`mmap(MAP_SHARED|MAP_ANONYMOUS)` gives the same virtual address to all threads.

In nginx's multi-process model, different processes have separate address spaces.
Two mechanisms are used depending on when the SAB is created:

---

## Pre-Fork SABs (MAP_SHARED|MAP_ANONYMOUS)

SABs allocated during `init_conf` (before any `fork()`) use:
```c
mmap(NULL, total, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0)
```

Because `fork()` inherits all mappings, **every worker process and the master
have this SAB at the same virtual address**.  No fd passing is needed; the VA
alone identifies the SAB.

The `ngx_js_sab_hdr_t` header embedded before the data buffer carries an atomic
`ref_count` used for lifetime management:
```c
typedef struct {
    int      ref_count;   /* atomic, shared across all processes */
    uint32_t flags;       /* NGX_JS_SAB_SHARED | NGX_JS_SAB_MEMFD */
    uint32_t size;        /* payload bytes */
    uint32_t _pad;
    uint64_t buf[0];      /* data region */
} ngx_js_sab_hdr_t;
```

When sending a pre-fork SAB over a channel, `channel_send` adds a **transit dup**
(`ngx_js_sab_dup`) to keep the pages alive until the receiver has called
`JS_ReadObject`.  The receiver calls `ngx_js_sab_free` to release the transit ref.

---

## Post-Fork SABs (memfd_create)

SABs allocated after fork — in worker processes or in SW pthreads — use:
```c
fd = memfd_create("ngx_js_sab", MFD_CLOEXEC);
ftruncate(fd, total);
ptr = mmap(NULL, total, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
```

The fd is kept open.  Another process receives the fd via `SCM_RIGHTS` on an
AF_UNIX socket and maps it at its own virtual address.  Both VAs map the same
physical pages — writes in one process are immediately visible in the other.

The `flags` field in the header is `NGX_JS_SAB_MEMFD` for these SABs.

---

## fd Table (ngx_js_sab_fd_table)

Each process maintains a static 64-entry table tracking its memfd SABs:

```c
typedef struct {
    void     *ptr;        /* data pointer (buf[]) in this process */
    int       fd;         /* memfd fd; -1 = unused slot */
    size_t    size;
    uint32_t  local_refs; /* ref count within this process */
} ngx_js_sab_fd_entry_t;

static ngx_js_sab_fd_entry_t  ngx_js_sab_fd_table[64];
static pthread_mutex_t         ngx_js_sab_fd_lock;
```

`local_refs` counts how many live JS references exist to this SAB in the current
process.  It is incremented by `ngx_js_sab_dup` (called by QuickJS when a SAB is
referenced) and decremented by `ngx_js_sab_free` (called by QuickJS GC).  When it
reaches 0, `munmap` and `close(fd)` are called.

---

## SAB Lifecycle for a Received memfd SAB

When `channel_recv` receives a message containing a memfd SAB:

```
channel_recv
  1. recvmsg receives fd via SCM_RIGHTS
  2. mmap(fd)  →  new_ptr (local VA)
  3. ngx_js_sab_register_memfd(new_ptr, fd, size)  →  local_refs = 1
  4. channel_patch_va(buf, sender_va → new_ptr)
  5. return sab_tab[i] = new_ptr

JS_ReadObject(buf)
  6. QuickJS reads BC_TAG_SHARED_ARRAY_BUFFER with patched VA
  7. js_array_buffer_constructor3(alloc_flag=FALSE, buf=new_ptr)
  8.   → sab_dup(new_ptr)  →  local_refs = 2

ngx_js_sab_free(sab_tab[i])   ← "transit ref release"
  9. local_refs = 2 → 1

JS code uses the SAB normally (local_refs = 1).

QuickJS GC collects the JS object
  10. js_array_buffer_finalizer  →  sab_free(new_ptr)
  11. local_refs = 1 → 0  →  munmap + close(fd)
```

The transit ref (step 1-3) pairs with the transit release (step 9).  The GC ref
(steps 7-8) pairs with the GC free (steps 10-11).  Both paths go through
`ngx_js_sab_dup` and `ngx_js_sab_free`, protected by `ngx_js_sab_fd_lock`.

---

## channel_patch_va

Received message buffers contain the **sender's virtual address** of each SAB
(as embedded by QuickJS in `BC_TAG_SHARED_ARRAY_BUFFER`).  The receiver has
mapped the memfd at a different VA.  `channel_patch_va` does a byte scan:

```c
for (i = 0; i + 8 <= len; i++) {
    if (*(uint64_t *)(buf + i) == old_va) {
        *(uint64_t *)(buf + i) = new_va;
    }
}
```

This rewrites all occurrences of the 8-byte sender VA to the 8-byte receiver VA
before `JS_ReadObject` is called.  After patching, QuickJS reads the receiver VA
and calls `sab_dup` on it, finding the correct fd table entry.

---

## Pre-Fork vs Post-Fork SABs in channel_send

`channel_send` inspects `sab_hdr->flags` for each SAB in `sab_tab`:

```c
if (sab_hdr->flags & NGX_JS_SAB_MEMFD) {
    /* send fd via SCM_RIGHTS; include {sender_va, size} in body */
    sab_fd = ngx_js_sab_get_fd(sab_tab[i]);
    memfd_fds[n_memfds]            = sab_fd;
    memfd_info[n_memfds].sender_va = (uint64_t)(uintptr_t) sab_tab[i];
    memfd_info[n_memfds].size      = sab_hdr->size;
    n_memfds++;

} else if (sab_hdr->flags & NGX_JS_SAB_SHARED) {
    /* pre-fork: transit dup; receiver uses same VA */
    ngx_js_sab_dup(NULL, sab_tab[i]);
    shared_vas[n_shared++] = (uint64_t)(uintptr_t) sab_tab[i];
}
```

The 16-byte message header encodes both `n_sabs` and `n_memfds` so the receiver
knows how many VAs and how many fd entries to expect.

---

## Atomics and the Futex

For inter-process `Atomics.wait` / `Atomics.notify`:

- **Pre-fork SABs**: same VA in master and worker → Linux `futex(2)` resolves
  both to the same physical page automatically.
- **Post-fork memfd SABs**: different VAs, same physical pages → `futex(2)` with
  `FUTEX_WAIT` (no `FUTEX_PRIVATE_FLAG`) uses the physical page as the key.

The SW thread has `JS_SetCanBlock(rt, TRUE)` so `Atomics.wait()` may call
`futex(FUTEX_WAIT, ...)` which blocks the pthread without affecting the nginx
event loop.  Workers use only `Atomics.notify()` which is non-blocking.

---

## Sending SABs from a SW Thread back to a Worker

When a SW thread calls `port.postMessage(sab)`:
1. `channel_send` from SW → worker: fd sent via SCM_RIGHTS (memfd path).
2. Worker's `channel_recv` mmap's the fd to local VA, registers with `local_refs=1`.
3. `JS_ReadObject` calls `sab_dup` → `local_refs=2`.
4. Transit release → `local_refs=1`.
5. Worker JS code can read/write the SAB normally.

The SW **retains** its own mapping (its fd table entry stays at `local_refs=1`).
Mutations made by the worker are visible in the SW's mapping immediately.
