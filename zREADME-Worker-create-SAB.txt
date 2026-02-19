● Three test cases in t/js_worker_creates_sab.t:

  ┌───────────────────────────┬──────────────────────────────────────────────────────────────────────────────────────────────────┐
  │           Route           │                                          What it tests                                           │
  ├───────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ /worker_creates_sab/      │ Worker creates SAB{buf[0]=42}; main reads buf[0] → 42                                            │
  ├───────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ /worker_creates_sab_pair/ │ Worker creates SAB{buf[0]=6, buf[1]=7}; main computes 6×7 → 42                                   │
  ├───────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ /worker_creates_writable/ │ Worker creates SAB{buf[0]=21}; main doubles it in-place → 42, confirming the mapping is writable │
  └───────────────────────────┴──────────────────────────────────────────────────────────────────────────────────────────────────┘

  No code changes were needed — the Worker thread runs post-fork in a worker process so ngx_js_sab_alloc already routes it through memfd, and QuickJS's existing intra-process worker pipe handles the
  serialization with sab_dup on receipt.
