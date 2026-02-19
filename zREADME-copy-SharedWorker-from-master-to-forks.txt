❯ just a question: how ee can prevent SharedWorkers created before fork in main worker, form be cloned to nginx workers where they are useless?

● Short answer: you can't prevent fork() from copying the address space, but you can minimise the damage in two ways.

  1. Close sw_fd in init_process

  After fork the worker has no business touching the SW thread's socket end. Walking jcf->sw_list in init_process and calling close(sw->sw_fd) (then setting it to -1) is defensive hygiene: it
  prevents accidental writes to the SW side and releases the fd from the worker's table. The worker_fd is the only fd the worker actually needs.

  2. Separate master-only state from worker-only state

  Right now the SW struct carries everything: pthread id, sw_fd, JS on_message value, the manager socket, etc. Workers need exactly one thing per SW: the worker_fd. A cleaner split would be:

  /* lives only in master */
  typedef struct {
      pthread_t        tid;
      int              sw_fd;       /* SW-thread end, closed after fork */
      JSValue          on_message;
      /* ... */
  } ngx_js_sw_master_t;

  /* inherited by workers (small, mostly read-only) */
  typedef struct {
      int              worker_fd;   /* worker's channel to the SW thread */
      ngx_str_t        url;         /* for new SharedWorker(url) lookup */
  } ngx_js_sw_slot_t;

  Workers never touch ngx_js_sw_master_t; COW keeps those pages uncopied in practice until written. The ngx_js_sw_slot_t array is small and intentionally shared.

  3. JS objects are already COW-free

  Workers inherit jcf->ctx (the master's JSContext). The JS SharedWorker wrapper objects live on heap pages that are COW-shared with the master. As long as workers never write to those pages (they
  don't mutate the master-side JS objects), no physical duplication occurs. They look "cloned" in the virtual address space but cost zero extra RAM.

  So the practical checklist is just: close sw_fd in init_process, keep the inherited struct small, and don't worry about the JS side — COW takes care of it.

✻ Crunched for 57s
