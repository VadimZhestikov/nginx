  Code changes (nginx/src/js/):

  - ngx_js_sw.c: added __thread int ngx_js_sw_thread_active;, set to 1 at the start of ngx_js_sw_thread
  - ngx_js.h: exported extern __thread int ngx_js_sw_thread_active;
  - ngx_js_module.c: ngx_js_sab_alloc now checks !ngx_js_sw_thread_active in the master-path condition — SW threads fall through to the memfd path so their SABs can be delivered to workers via
  SCM_RIGHTS

  Test (t/js_sw_sab_to_worker.t):
  - SW script sw_make_sab.js: on each message, creates a fresh SharedArrayBuffer, writes the received value into buf[0], posts it back
  - Two routes (/sw_creates_sab/ with value 42, /sw_creates_sab2/ with value 99), each tested 3× (200 OK + value + repeat)
  - 8/8 tests pass; full suite 240/240
