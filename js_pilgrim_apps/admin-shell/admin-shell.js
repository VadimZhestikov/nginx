/*
 * admin-shell.js — JS-Pilgrim P19 Admin Shell
 *
 * A browser-accessible admin UI for the nginx JS runtime.
 *
 * Protocol: WebSocket (RFC 6455) over HTTP upgrade.
 * Messages: JSON-RPC 2.0 text frames.
 *
 * Methods:
 *   plugins.list            → {result: [...paths]}
 *   shared.get(key)         → {result: value}
 *   shared.set(key, value)  → {result: "ok"}
 *   shared.delete(key)      → {result: "ok"}
 *   shared.keys()           → {result: [...keys]}
 *   nginx.eval(code)        → {result: {status, value, message, stack}}
 *   upstreams.list()        → {result: [...upstream objects]}
 *
 * Usage in nginx JS config:
 *   nginx.use('js_pilgrim_apps/admin-shell');
 */

(function() {

/* ================================================================== */
/* Relay SharedWorker — routes nginx.eval to target worker             */
/* ================================================================== */

var relay         = new SharedWorker(nginx.cycle.installPrefix +
                        'js_pilgrim_apps/admin-shell/repl-relay.js');
var relayPending  = {};   /* token → function(resultObj) */

/*
 * nginx.broadcast() runs once per worker after fork so each worker
 * registers itself and receives eval requests for its workerIdx.
 */
nginx.broadcast(function() {
    relay.onmessage = function(e) {
        var msg = e.data;
        if (!msg) { return; }

        if (msg.type === 'eval') {
            /* We are the target — evaluate and send result back */
            relay.postMessage({
                type:        'result',
                token:       msg.token,
                result:      nginx.repl.eval(msg.line),
                replyWorker: msg.replyWorker,
            });
        } else if (msg.type === 'result') {
            /* We are the gateway — deliver result to waiting RPC cb */
            var cb = relayPending[msg.token];
            if (cb) { delete relayPending[msg.token]; cb(msg.result); }
        }
    };

    nginx.setTimeout(0).then(function() {
        relay.postMessage({ type: 'register', workerId: nginx.workerIdx });
    });
});

/* ================================================================== */
/* SHA-1 (RFC 3174) — pure JS, needed for WebSocket handshake          */
/* ================================================================== */

function sha1(bytes) {
    var H = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0];

    function u32(n) { return n >>> 0; }
    function rol(n, s) { return u32((n << s) | (n >>> (32 - s))); }
    function add(a, b) { return u32(a + b); }

    var msg = bytes.slice();
    var len = msg.length;
    msg.push(0x80);
    while ((msg.length % 64) !== 56) { msg.push(0); }

    /* Append 64-bit big-endian bit count */
    var bits = len * 8;
    for (var i = 7; i >= 0; i--) {
        msg.push(Math.floor(bits / Math.pow(2, i * 8)) & 0xFF);
    }

    for (var blk = 0; blk < msg.length; blk += 64) {
        var W = [];
        for (var j = 0; j < 16; j++) {
            W[j] = (msg[blk+j*4]   << 24) | (msg[blk+j*4+1] << 16) |
                   (msg[blk+j*4+2] <<  8) |  msg[blk+j*4+3];
        }
        for (var j = 16; j < 80; j++) {
            W[j] = rol(u32(W[j-3] ^ W[j-8] ^ W[j-14] ^ W[j-16]), 1);
        }

        var a = H[0], b = H[1], c = H[2], d = H[3], e = H[4];

        for (var j = 0; j < 80; j++) {
            var f, k;
            if      (j < 20) { f = u32((b & c) | ((~b) & d)); k = 0x5A827999; }
            else if (j < 40) { f = u32(b ^ c ^ d);             k = 0x6ED9EBA1; }
            else if (j < 60) { f = u32((b&c)|(b&d)|(c&d));     k = 0x8F1BBCDC; }
            else             { f = u32(b ^ c ^ d);              k = 0xCA62C1D6; }

            var temp = add(add(add(add(rol(a, 5), f), e), u32(k)), u32(W[j]));
            e = d; d = c; c = rol(b, 30); b = a; a = temp;
        }

        H[0] = add(H[0], a); H[1] = add(H[1], b); H[2] = add(H[2], c);
        H[3] = add(H[3], d); H[4] = add(H[4], e);
    }

    var out = [];
    for (var i = 0; i < 5; i++) {
        out.push((H[i]>>>24)&0xFF, (H[i]>>>16)&0xFF,
                 (H[i]>>> 8)&0xFF,  H[i]      &0xFF);
    }
    return out;
}

/* ================================================================== */
/* Base64 encode                                                        */
/* ================================================================== */

var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

function base64enc(bytes) {
    var out = '';
    for (var i = 0; i < bytes.length; i += 3) {
        var n = (bytes[i] << 16) | ((bytes[i+1] || 0) << 8) | (bytes[i+2] || 0);
        out += B64[(n>>18)&63] + B64[(n>>12)&63];
        out += (i+1 < bytes.length) ? B64[(n>>6)&63] : '=';
        out += (i+2 < bytes.length) ? B64[n&63]      : '=';
    }
    return out;
}

/* ================================================================== */
/* WebSocket helpers                                                    */
/* ================================================================== */

var WS_MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

function wsAccept(key) {
    var src = key + WS_MAGIC;
    var bytes = [];
    for (var i = 0; i < src.length; i++) { bytes.push(src.charCodeAt(i)); }
    return base64enc(sha1(bytes));
}

/* Build a WebSocket text frame (FIN=1, opcode=1, no masking — server→client) */
function wsTextFrame(text) {
    var payload = [];
    for (var i = 0; i < text.length; i++) {
        var c = text.charCodeAt(i);
        if (c < 0x80) {
            payload.push(c);
        } else if (c < 0x800) {
            payload.push(0xC0 | (c >> 6), 0x80 | (c & 0x3F));
        } else {
            payload.push(0xE0 | (c >> 12),
                         0x80 | ((c >> 6) & 0x3F),
                         0x80 | (c & 0x3F));
        }
    }
    var len = payload.length;
    var header = [0x81];  /* FIN=1, opcode=1 (text) */
    if (len < 126) {
        header.push(len);
    } else if (len < 65536) {
        header.push(126, (len >> 8) & 0xFF, len & 0xFF);
    } else {
        header.push(127, 0, 0, 0, 0,
                    (len >> 24) & 0xFF, (len >> 16) & 0xFF,
                    (len >>  8) & 0xFF,  len         & 0xFF);
    }
    return new Uint8Array(header.concat(payload));
}

/* Build a WebSocket close frame */
function wsCloseFrame(code) {
    code = code || 1000;
    return new Uint8Array([0x88, 2, (code >> 8) & 0xFF, code & 0xFF]);
}

/* ================================================================== */
/* Per-connection state                                                 */
/* ================================================================== */

var wsConns = {};          /* fd → {buf: number[], pending: string} */

function wsConnInit(fd, targetWorker) {
    wsConns[fd] = { buf: [], pending: '',
                    targetWorker: targetWorker !== undefined
                                  ? targetWorker : nginx.workerIdx };
}

function wsConnFree(fd) {
    delete wsConns[fd];
}

/* ================================================================== */
/* WebSocket frame parser                                               */
/* ================================================================== */

/*
 * Parse one complete WebSocket frame from buf starting at offset.
 * Returns {opcode, payload (number[]), fin, consumed} or null if incomplete.
 */
function parseWsFrame(buf, offset) {
    if (buf.length - offset < 2) { return null; }

    var b0      = buf[offset];
    var b1      = buf[offset + 1];
    var fin     = (b0 & 0x80) !== 0;
    var opcode  = b0 & 0x0F;
    var masked  = (b1 & 0x80) !== 0;
    var payLen  = b1 & 0x7F;
    var pos     = offset + 2;

    if (payLen === 126) {
        if (buf.length - pos < 2) { return null; }
        payLen = (buf[pos] << 8) | buf[pos + 1];
        pos += 2;
    } else if (payLen === 127) {
        if (buf.length - pos < 8) { return null; }
        /* Use only the low 32 bits — adequate for control messages */
        payLen = (buf[pos+4] << 24) | (buf[pos+5] << 16) |
                 (buf[pos+6] <<  8) |  buf[pos+7];
        pos += 8;
    }

    var maskLen = masked ? 4 : 0;
    if (buf.length - pos < maskLen + payLen) { return null; }

    var mask = masked ? buf.slice(pos, pos + 4) : null;
    pos += maskLen;

    var payload = [];
    for (var i = 0; i < payLen; i++) {
        payload.push(masked ? buf[pos + i] ^ mask[i % 4] : buf[pos + i]);
    }

    return { opcode: opcode, fin: fin, payload: payload,
             consumed: pos + payLen - offset };
}

/* ================================================================== */
/* tree.get helper — returns a type-annotated description of a value   */
/* ================================================================== */

function treeNodeInfo(val) {
    var t = typeof val;
    if (val === null)      { return { kind: 'null' }; }
    if (t === 'undefined') { return { kind: 'undefined' }; }
    if (t === 'boolean')   { return { kind: 'boolean', value: val }; }
    if (t === 'number')    { return { kind: 'number',  value: val }; }
    if (t === 'string')    { return { kind: 'string',  value: val }; }
    if (t === 'function')  { return { kind: 'function', name: val.name || '' }; }
    if (Array.isArray(val)) {
        return { kind: 'array', length: val.length };
    }
    if (t === 'object') {
        var keys = [];
        try { keys = Object.keys(val); } catch (e) { /* opaque C object */ }
        return { kind: 'object', keys: keys };
    }
    return { kind: t };
}

/* ================================================================== */
/* JSON-RPC dispatcher                                                  */
/* ================================================================== */

function rpcError(id, code, message) {
    return JSON.stringify({ jsonrpc: '2.0', id: id,
                            error: { code: code, message: message } });
}

function rpcOk(id, result) {
    return JSON.stringify({ jsonrpc: '2.0', id: id, result: result });
}

function dispatch(msg, fd) {
    var req;
    try {
        req = JSON.parse(msg);
    } catch (e) {
        return rpcError(null, -32700, 'Parse error');
    }

    var id     = (req.id !== undefined) ? req.id : null;
    var method = req.method;
    var params = req.params || [];

    if (!method) {
        return rpcError(id, -32600, 'Invalid Request: method required');
    }

    var conn         = wsConns[fd];
    var targetWorker = conn ? conn.targetWorker : nginx.workerIdx;

    try {
        switch (method) {

        case 'plugins.list':
            return rpcOk(id, nginx.plugins.slice());

        case 'shared.get':
            return rpcOk(id, nginx.shared.get(params[0]));

        case 'shared.set':
            nginx.shared.set(params[0], params[1]);
            return rpcOk(id, 'ok');

        case 'shared.delete':
            nginx.shared['delete'](params[0]);
            return rpcOk(id, 'ok');

        case 'shared.keys':
            return rpcOk(id, nginx.shared.keys());

        case 'nginx.eval':
            if (targetWorker === nginx.workerIdx) {
                /* Local eval */
                return rpcOk(id, nginx.repl.eval(params[0]));
            }
            /* Cross-worker eval via relay — async, no immediate return */
            (function() {
                var token = 'ws-' + fd + '-' + id;
                relayPending[token] = function(result) {
                    var frame = wsTextFrame(rpcOk(id, result));
                    nginx.repl._writeFdRaw(fd, frame);
                };
                relay.postMessage({
                    type:         'eval',
                    token:        token,
                    line:         params[0],
                    targetWorker: targetWorker,
                    replyWorker:  nginx.workerIdx,
                });
            }());
            return null;  /* response sent asynchronously */

        case 'worker.id':
            return rpcOk(id, targetWorker);

        case 'tree.get': {
            var expr = String(params[0] || 'nginx');
            var val;
            try {
                val = (new Function('return (' + expr + ')'))();
            } catch (e) {
                return rpcError(id, -32000, 'Eval error: ' + String(e));
            }
            return rpcOk(id, treeNodeInfo(val));
        }

        case 'upstreams.list': {
            var upstreams = nginx.http.upstreams;
            var list = [];
            for (var i = 0; i < upstreams.length; i++) {
                var us = upstreams[i];
                var peers = [];
                for (var j = 0; j < us.peers.length; j++) {
                    var p = us.peers[j];
                    peers.push({
                        address:  p.address,
                        weight:   p.weight,
                        down:     p.down,
                        maxFails: p.maxFails
                    });
                }
                list.push({ name: us.name, peers: peers });
            }
            return rpcOk(id, list);
        }

        default:
            return rpcError(id, -32601, 'Method not found: ' + method);
        }
    } catch (e) {
        return rpcError(id, -32603, 'Internal error: ' + String(e));
    }
}

/* ================================================================== */
/* WebSocket message handler                                            */
/* ================================================================== */

function onWsMessage(fd, text) {
    var response = dispatch(text, fd);
    if (response !== null) {
        nginx.repl._writeFdRaw(fd, wsTextFrame(response));
    }
}

/* ================================================================== */
/* WebSocket raw-data handler (called per TCP segment)                  */
/* ================================================================== */

function onWsData(fd, chunk) {
    var conn = wsConns[fd];
    if (!conn) { return; }

    /* Append chunk bytes to per-connection buffer */
    for (var i = 0; i < chunk.length; i++) {
        conn.buf.push(chunk[i]);
    }

    /* Process all complete frames */
    var offset = 0;
    for (;;) {
        var frame = parseWsFrame(conn.buf, offset);
        if (!frame) { break; }

        offset += frame.consumed;

        if (frame.opcode === 8) {
            /* Close frame — echo and clean up */
            nginx.repl._writeFdRaw(fd, wsCloseFrame(1000));
            wsConnFree(fd);
            return;
        }

        if (frame.opcode === 9) {
            /* Ping — reply with Pong (opcode 10) */
            var pong = new Uint8Array([0x8A, 0]);
            nginx.repl._writeFdRaw(fd, pong);
            continue;
        }

        if (frame.opcode === 1 || frame.opcode === 0) {
            /* Text or continuation frame */
            var part = '';
            for (var i = 0; i < frame.payload.length; i++) {
                part += String.fromCharCode(frame.payload[i]);
            }
            conn.pending += part;

            if (frame.fin) {
                onWsMessage(fd, conn.pending);
                conn.pending = '';
            }
        }
    }

    /* Keep only unprocessed bytes */
    if (offset > 0) {
        conn.buf = conn.buf.slice(offset);
    }
}

/* ================================================================== */
/* HTTP Upgrade handler — installed on the WebSocket location           */
/* ================================================================== */

function wsUpgradeHandler(req) {
    var upgrade = req.headers['upgrade'];
    var wsKey   = req.headers['sec-websocket-key'];
    var connHdr = req.headers['connection'] || '';

    if (!upgrade || upgrade.toLowerCase() !== 'websocket' ||
        !wsKey   || connHdr.toLowerCase().indexOf('upgrade') === -1)
    {
        req.respond(400, { 'Content-Type': 'text/plain' },
                    'WebSocket upgrade required\n');
        return;
    }

    var accept = wsAccept(wsKey);
    var fd     = req.hijack();

    var resp = 'HTTP/1.1 101 Switching Protocols\r\n' +
               'Upgrade: websocket\r\n' +
               'Connection: Upgrade\r\n' +
               'Sec-WebSocket-Accept: ' + accept + '\r\n\r\n';

    var qw = req.queryParams && req.queryParams.w;
    var targetWorker = (qw !== undefined && qw !== '')
                       ? parseInt(qw, 10) : nginx.workerIdx;

    nginx.repl._writeFd(fd, resp);
    wsConnInit(fd, targetWorker);

    nginx.repl.listenRaw(fd, function(chunk) {
        onWsData(fd, chunk);
    });
}

/* ================================================================== */
/* Installation                                                         */
/* ================================================================== */

(function install() {
    var servers = nginx.http.servers;
    for (var si = 0; si < servers.length; si++) {
        var locs = servers[si].locations;
        for (var li = 0; li < locs.length; li++) {
            var loc = locs[li];
            if (loc.path === '/admin/ws' || loc.path === '/admin/ws/') {
                loc.handler = wsUpgradeHandler;
            }
        }
    }
}());

}());
