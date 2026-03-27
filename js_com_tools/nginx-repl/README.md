# nginx JS REPL

An interactive JavaScript REPL that evaluates code inside a running nginx worker
process via the JS COM API.  Supports multi-worker deployments: you can target
any specific worker and get the result of evaluating JS in that worker's live
context.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  nginx master process                                   │
│  ┌─────────────────────────────────────────────────┐   │
│  │  SharedWorker thread  (repl-relay.js)           │   │
│  │  Routes eval messages between workers.          │   │
│  │  Maintains workerId → port map.                 │   │
│  └──────────────────────────────────────────────── ┘   │
│       │ AF_UNIX SOCK_SEQPACKET                          │
│  ┌────┴──────┐  ┌────────────┐  ┌────────────┐         │
│  │ worker 0  │  │  worker 1  │  │  worker 2  │  …      │
│  │ (gateway) │  │            │  │ (target)   │         │
│  └─────┬─────┘  └────────────┘  └────────────┘         │
└────────┼────────────────────────────────────────────────┘
         │ TCP (HTTP upgrade → nginx-repl protocol)
    repl-client.py / repl-client.js
```

Any worker can act as the **gateway**: it hijacks the HTTP connection and uses
the relay SharedWorker to forward `EVAL` commands to the chosen **target**
worker.  If the gateway IS the target (default), no relay is needed.

## Files

| File                          | Purpose                                         |
|-------------------------------|-------------------------------------------------|
| `nginx-repl-demo/nginx.conf`  | 4-worker nginx config on 127.0.0.1:8099         |
| `nginx-repl-demo/repl.js`     | JS handler: hijacks connection, wires relay     |
| `nginx-repl-demo/repl-relay.js` | SharedWorker that routes cross-worker EVALs   |
| `repl-client.py`              | Interactive Python client (recommended)         |
| `repl-client.js`              | Interactive QuickJS client (`qjs --std`)        |
| `run-example.txt`             | Quick-start commands                            |

## Quick start

```bash
# 1. Build (from repo root)
make -C quickjs libquickjs.a
cd nginx && auto/configure \
  --add-module=../nginx/src/js \
  --with-cc-opt="-I../quickjs -Wno-cast-function-type" \
  --with-ld-opt="-L../quickjs -lquickjs -lm" \
  --with-http_ssl_module \
  --with-stream
make -j$(nproc)
cd ..

# 2. Start nginx
mkdir -p tools/nginx-repl-demo/logs
nginx/objs/nginx -p tools -c nginx-repl-demo/nginx.conf

# 3. Connect (any worker)
python3 tools/repl-client.py

# 4. Connect to a specific worker (0-based)
python3 tools/repl-client.py 127.0.0.1 8099 /repl/ 2

# 5. Stop nginx
kill $(cat tools/nginx-repl-demo/nginx.pid)
```

## Session example

```
$ python3 tools/repl-client.py 127.0.0.1 8099 /repl/ 2
nginx JS REPL  127.0.0.1:8099/repl/  → worker 2
Type JS to evaluate.  Ctrl-D to exit.

>>> nginx.workerIdx
2
>>> nginx.version
"1.29.6"
>>> nginx.http.servers.map(s => s.name)
["localhost"]
>>> nginx.http.servers[0].locations.map(l => l.path)
["/repl/"]
>>> 1 + 2
3
>>> var x = 0; nginx.setTimeout(100).then(() => x = 42); "timer set"
"timer set"
>>> x
42
```

## Wire protocol

The REPL uses a plain-text, line-oriented protocol over a raw TCP connection
upgraded from HTTP.

### Handshake

```
→  GET /repl/?w=2&loglevel=2&nginxlevel=4 HTTP/1.1
   Connection: upgrade
   Upgrade: nginx-repl

←  HTTP/1.1 101 Switching Protocols
   Connection: Upgrade
   Upgrade: nginx-repl
```

Query parameters:

| Parameter    | Default | Meaning                                       |
|--------------|---------|-----------------------------------------------|
| `w`          | *(none)* | Target worker index (0-based).  Omit for the gateway worker. |
| `loglevel`   | 0       | `console.*` verbosity: 0=off 1=error 2=warn 3=info 4=debug  |
| `nginxlevel` | 0       | `nginx.log` verbosity mirrored to the REPL: same scale      |

### Client → server

```
<token> EVAL <js-expression-or-statement>\n
<token> DETACH\n
```

`token` is any whitespace-free string used to match replies (e.g. `t1`, `t2`).

### Server → client

```
<token> OK [<value>]\n          — success; value is the string representation
<token> INCOMPLETE\n            — JS is syntactically incomplete (multiline)
<token> ERR <message>\n         — runtime or syntax error
<token> STACK <frame>\n         — one stack frame (follows ERR)
<token> STACK_END\n             — end of stack trace
* LOG console:<level> <msg>\n   — console.log/warn/error output
* LOG nginx:<level>   <msg>\n   — nginx.log output
```

### Multiline input

The Python client accumulates `INCOMPLETE` responses and re-sends the growing
buffer until the expression is syntactically complete.  You can enter multiline
JS naturally:

```
>>> function add(a, b) {
...   return a + b;
... }
>>> add(1, 2)
3
```

## Embedding in your own nginx config

Add these lines to your `nginx.conf` and `js_source` script to expose a REPL
endpoint on an existing server:

```nginx
# nginx.conf
js_source conf/repl.js;

http {
    server {
        listen 127.0.0.1:8099;    # bind to loopback only
        location /repl/ { }       # handler installed by repl.js
    }
}
```

```js
// conf/repl.js  (minimal, no cross-worker relay)
var loc = nginx.http.servers[0].locations.find(l => l.path === '/repl/');
loc.handler = function (req) {
    var fd = req.hijack();
    nginx.repl._writeFd(fd,
        'HTTP/1.1 101 Switching Protocols\r\n' +
        'Connection: Upgrade\r\nUpgrade: nginx-repl\r\n\r\n');
    nginx.repl.attach(fd, 2, 4);
    nginx.repl.listen(fd, function (line) {
        var sp  = line.indexOf(' ');
        var tok = line.slice(0, sp);
        var rest = line.slice(sp + 1);
        if (rest.startsWith('EVAL ')) {
            var r = nginx.repl.eval(rest.slice(5));
            var out = r.status === 'ok'
                      ? tok + ' OK ' + (r.value != null ? r.value : '') + '\n'
                      : tok + ' ERR ' + r.message + '\n';
            nginx.repl._writeFd(fd, out);
        }
    });
};
```

For cross-worker routing copy `nginx-repl-demo/repl.js` and
`nginx-repl-demo/repl-relay.js` into your project.

## Security

The REPL grants full `nginx` JS COM access to anyone who can connect.
**Always bind to loopback (`127.0.0.1`) and protect with firewall rules or
an `allow`/`deny` block.**  Never expose port 8099 to the internet.

```nginx
location /repl/ {
    allow 127.0.0.1;
    deny  all;
}
```
