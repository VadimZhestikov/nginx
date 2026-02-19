● The test covers two distinct sharing mechanisms:

  /sw_prefork_atomics/ — pre-fork SAB (same VA in master and worker). The SW's Atomics.wait(arr, 0, 0) blocks on the physical page. The worker writes arr[1]=42, sets arr[0]=1, then Atomics.notify —
  futex uses the physical page identity so the SW wakes correctly.

  /sw_worker_atomics/ — worker-created SAB (memfd, different VAs in worker and master). This is the interesting cross-process case: FUTEX_WAIT (shared mode, not _PRIVATE) resolves the virtual address
   to a physical page key, so notify from the worker at VA X wakes the SW waiting at a different VA Y — both map to the same physical page at the same offset.

  One design note: the "not-equal" return from Atomics.wait is a legitimate fast-path (worker already set arr[0]=1 before SW called wait) and is handled correctly — the SW reads arr[1] regardless.

