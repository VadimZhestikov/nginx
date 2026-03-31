# 12 — Wire Formats and Internal Protocols

---

## 1. SharedWorker Channel Protocol

All messages between nginx workers and a SharedWorker thread travel over AF_UNIX
`SOCK_SEQPACKET` socketpairs.  Each message is a single `sendmsg` / `recvmsg`
call with atomic delivery (no fragmentation at the kernel level).

### Message Header (16 bytes, always present)
```
Offset  Size  Field
0       4     type       uint32_t  — message type constant
4       4     data_len   uint32_t  — bytes of JS-serialised payload
8       4     n_sabs     uint32_t  — pre-fork SABs (shared_vas count)
12      4     n_memfds   uint32_t  — post-fork memfd SABs (memfd_info count)
```

### Message Body (variable, after the header)
```
[data_len bytes]   QuickJS JS_WriteObject serialised payload
[n_sabs × 8]       uint64_t[] — sender virtual addresses of pre-fork SABs
[n_memfds × 24]    ngx_js_sw_memfd_info_t[] — {sender_va:u64, size:u32, pad:u32}
```

If `n_memfds > 0`, the `sendmsg` also carries an `SCM_RIGHTS` ancillary data
block with `n_memfds` file descriptors (one memfd per SAB).

### ngx_js_sw_memfd_info_t
```c
typedef struct {
    uint64_t  sender_va;   /* data pointer in the sending process */
    uint32_t  size;        /* payload bytes */
    uint32_t  _pad;
} ngx_js_sw_memfd_info_t;
```

### Message Type Constants
```c
#define NGX_JS_SW_MSG_CONNECT  0
#define NGX_JS_SW_MSG_DATA     1
#define NGX_JS_SW_MSG_TERM     2
```

### Wake Pipe
Each SW has a `wake_pipe[2]` separate from the data socketpair.  After a worker
sends a DATA message, it writes one byte (`channel_index & 0xff`) to
`wake_pipe[1]`.  The SW thread `poll()`s `wake_pipe[0]` (not the socketpair)
for reliability on WSL2.

---

## 2. Manager Thread Protocol (Dynamic SW Creation)

When a worker calls `new SharedWorker(url)` post-fork, it sends a request to the
manager thread over `sw_cmd_fds` (a global AF_UNIX SOCK_SEQPACKET pair).

### Worker → Manager Request
```
[url_len : uint32_t]
[worker_idx : uint32_t]
[url : url_len bytes]
SCM_RIGHTS: one fd (the "reply socket" — a second AF_UNIX socketpair half)
```

### Manager → Worker Reply
```
[status : uint32_t]   0 = success
SCM_RIGHTS: two fds
  fds[0] = worker_fd  (worker's end of the new channel)
  fds[1] = wake_pipe[1]  (write end of the SW's wake pipe)
```

The worker receives two fds.  `worker_fd` goes into its `ngx_js_channel_t`.
`wake_fd` (`wake_pipe[1]`) is stored in the `ngx_js_wt_sw_t.wake_fd` for JS
Worker threads, or in the dynamic SW stub's `wake_pipe[1]` for nginx workers.

---

## 3. Broadcast Protocol (Master → Workers)

Each worker has a dedicated AF_UNIX SOCK_STREAM socketpair for master → worker
messages:

```c
/* master side: */
ngx_channel_t  ch;
ch.command = NGX_CMD_JS_BCAST;
/* payload written after the standard ngx_channel_t header */
ngx_write_channel(w->channel[0], &ch, ...);
```

The payload is a QuickJS `JS_WriteObject`-serialised JS value (the argument
passed to `nginx.broadcast(msg)`).

Workers watch their channel read fd via epoll.  On data-ready:
1. Read the serialised payload.
2. Call `JS_ReadObject`.
3. Fire each registered `nginx.on('message', fn)` callback with the deserialised
   value.

Special broadcast sub-types (socket distribution, suspend, resume) use specific
command codes recognised by the channel handler before the JS deserialisation step.

---

## 4. JS_WriteObject / JS_ReadObject Format

QuickJS's serialisation format is used for all inter-process and inter-thread
message passing.  Key characteristics:

- **Compact binary format** — not JSON, not MessagePack.
- **Supports SharedArrayBuffer**: `BC_TAG_SHARED_ARRAY_BUFFER` embeds a raw
  pointer (8 bytes) and byte length.  The receiver patches the pointer after
  mmap via `channel_patch_va`.
- **Object references** (`JS_WRITE_OBJ_REFERENCE`): the same object appearing
  multiple times in the graph is written once; subsequent occurrences are
  `BC_TAG_OBJECT_REFERENCE` indices.
- **Functions are not serialisable** by default; they must not appear in
  `postMessage` payloads.

Flags used in JS_Pilgrim:
- `JS_WRITE_OBJ_SAB` — permit SharedArrayBuffer in output
- `JS_READ_OBJ_SAB` — permit SharedArrayBuffer in input
- `JS_WRITE_OBJ_REFERENCE` / `JS_READ_OBJ_REFERENCE` — enable object graph sharing

---

## 5. WebSocket Protocol (Admin Shell)

The admin-shell implements RFC 6455 in pure JS.  Frame format:

### WebSocket Frame Header
```
Byte 0:  FIN(1) RSV1(1) RSV2(1) RSV3(1) OPCODE(4)
Byte 1:  MASK(1) PAYLOAD_LEN(7)
  If PAYLOAD_LEN == 126:  2 more bytes (uint16 extended length)
  If PAYLOAD_LEN == 127:  8 more bytes (uint64 extended length)
  If MASK bit set:        4 bytes masking key
```

Opcodes:
| Opcode | Name |
|---|---|
| 0x0 | Continuation |
| 0x1 | Text frame |
| 0x2 | Binary frame |
| 0x8 | Close |
| 0x9 | Ping |
| 0xA | Pong |

Server-to-client frames are unmasked (FIN=1, no mask key).
Client-to-server frames are masked (browsers always mask).

The admin-shell sends all responses as text frames (opcode 0x1) containing
JSON-RPC 2.0 objects.

---

## 6. JSON-RPC 2.0 Method Interface

### Request
```json
{"jsonrpc": "2.0", "id": 1, "method": "nginx.eval", "params": {"code": "nginx.version", "worker": 0}}
```

### Success Response
```json
{"jsonrpc": "2.0", "id": 1, "result": {"status": "ok", "value": "\"1.29.7\""}}
```

### Error Response
```json
{"jsonrpc": "2.0", "id": 1, "error": {"code": -32603, "message": "eval error: ReferenceError: …"}}
```

### nginx.eval Result Object
| `status` | Meaning |
|---|---|
| `"ok"` | Expression evaluated without error; `value` = JSON-stringified result |
| `"error"` | Evaluation threw; `message` = error message, `stack` = stack trace |
| `"incomplete"` | Input is syntactically incomplete; send more lines |

---

## 7. SAB Header Layout in Memory

For a `new SharedArrayBuffer(N)`:

```
[  ngx_js_sab_hdr_t (16 bytes)  ][  N bytes data  ]
  ^                                ^
  mmap base address               buf[] == pointer returned to QuickJS
```

`munmap` is called on the mmap base address (`hdr`), not on `buf[]`.  `close(fd)`
is called on the memfd.  Both happen when `local_refs` drops to 0 in
`ngx_js_sab_free`.

Alignment: the 16-byte header ensures `buf[]` starts at offset 16, which is
16-byte aligned for any reasonable malloc alignment.  This satisfies
`Int32Array`, `Float64Array`, etc.
