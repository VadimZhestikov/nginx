// A2.8 — Live Header Mutation Visible Over a Live WebSocket
//
// Story: open http://127.0.0.1:8117/ in a browser, watch the "X-Api-Header"
// value.  Then from a terminal:
//
//   curl -X POST "http://127.0.0.1:8117/admin/set-header/?value=v2-premium"
//
// The browser updates in < 300 ms without reconnecting.  All 4 workers serve
// the new value on their very next request — no nginx -s reload.
//
// Two primitives do all the work:
//   nginx.shared  — lock-free KV; any worker writes, all workers read
//                   instantly (no IPC, no signal, no reload).
//   nginx.broadcast(fn) — queued once, runs in every worker's init_process.
//
// WebSocket is a persistent plain TCP connection; frames are built in JS
// using the RFC 6455 framing (FIN | opcode | length | payload).
// The browser polls every 300 ms; it reads the current nginx.shared value
// and sends it back as a JSON push.

// ── SHA-1 (RFC 3174) — for Sec-WebSocket-Accept during the HTTP 101 handshake

function sha1(bytes) {
    var H = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0];
    function u32(n)    { return n >>> 0; }
    function rol(n, s) { return u32((n << s) | (n >>> (32 - s))); }
    function add(a, b) { return u32(a + b); }
    var msg = bytes.slice(), len = msg.length;
    msg.push(0x80);
    while ((msg.length % 64) !== 56) msg.push(0);
    var bits = len * 8;
    for (var i = 7; i >= 0; i--) msg.push(Math.floor(bits / Math.pow(2, i * 8)) & 0xFF);
    for (var blk = 0; blk < msg.length; blk += 64) {
        var W = [];
        for (var j = 0; j < 16; j++)
            W[j] = (msg[blk+j*4]<<24)|(msg[blk+j*4+1]<<16)|(msg[blk+j*4+2]<<8)|msg[blk+j*4+3];
        for (var j = 16; j < 80; j++)
            W[j] = rol(u32(W[j-3]^W[j-8]^W[j-14]^W[j-16]), 1);
        var a=H[0],b=H[1],c=H[2],d=H[3],e=H[4];
        for (var j = 0; j < 80; j++) {
            var f, k;
            if      (j<20){f=u32((b&c)|((~b)&d));k=0x5A827999;}
            else if (j<40){f=u32(b^c^d);          k=0x6ED9EBA1;}
            else if (j<60){f=u32((b&c)|(b&d)|(c&d));k=0x8F1BBCDC;}
            else          {f=u32(b^c^d);           k=0xCA62C1D6;}
            var tmp=add(add(add(add(rol(a,5),f),e),u32(k)),u32(W[j]));
            e=d;d=c;c=rol(b,30);b=a;a=tmp;
        }
        H[0]=add(H[0],a);H[1]=add(H[1],b);H[2]=add(H[2],c);H[3]=add(H[3],d);H[4]=add(H[4],e);
    }
    var out=[];
    for(var i=0;i<5;i++)
        out.push((H[i]>>>24)&0xFF,(H[i]>>>16)&0xFF,(H[i]>>>8)&0xFF,H[i]&0xFF);
    return out;
}

// ── Base64 encode

var _B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function base64enc(bytes) {
    var out='';
    for(var i=0;i<bytes.length;i+=3){
        var n=(bytes[i]<<16)|((bytes[i+1]||0)<<8)|(bytes[i+2]||0);
        out+=_B64[(n>>18)&63]+_B64[(n>>12)&63];
        out+=(i+1<bytes.length)?_B64[(n>>6)&63]:'=';
        out+=(i+2<bytes.length)?_B64[n&63]:'=';
    }
    return out;
}

// ── WebSocket frame helpers (server→client: no masking per RFC 6455 §5.1)

var _WS_MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

function wsAccept(key) {
    var src = key + _WS_MAGIC, bytes = [];
    for(var i=0;i<src.length;i++) bytes.push(src.charCodeAt(i));
    return base64enc(sha1(bytes));
}

function wsTextFrame(text) {
    var payload=[];
    for(var i=0;i<text.length;i++){
        var c=text.charCodeAt(i);
        if(c<0x80)       payload.push(c);
        else if(c<0x800) payload.push(0xC0|(c>>6), 0x80|(c&0x3F));
        else             payload.push(0xE0|(c>>12), 0x80|((c>>6)&0x3F), 0x80|(c&0x3F));
    }
    var len=payload.length, hdr=[0x81];
    if     (len<126)    hdr.push(len);
    else if(len<65536)  hdr.push(126,(len>>8)&0xFF,len&0xFF);
    else                hdr.push(127,0,0,0,0,(len>>24)&0xFF,(len>>16)&0xFF,(len>>8)&0xFF,len&0xFF);
    return new Uint8Array(hdr.concat(payload));
}

function wsCloseFrame(code) {
    code=code||1000;
    return new Uint8Array([0x88,2,(code>>8)&0xFF,code&0xFF]);
}

function parseWsFrame(buf, offset) {
    if(buf.length-offset<2) return null;
    var b0=buf[offset],b1=buf[offset+1];
    var fin=(b0&0x80)!==0, opcode=b0&0x0F, masked=(b1&0x80)!==0, payLen=b1&0x7F;
    var pos=offset+2;
    if(payLen===126){
        if(buf.length-pos<2) return null;
        payLen=(buf[pos]<<8)|buf[pos+1]; pos+=2;
    } else if(payLen===127){
        if(buf.length-pos<8) return null;
        payLen=(buf[pos+4]<<24)|(buf[pos+5]<<16)|(buf[pos+6]<<8)|buf[pos+7]; pos+=8;
    }
    var mlen=masked?4:0;
    if(buf.length-pos<mlen+payLen) return null;
    var mask=masked?buf.slice(pos,pos+4):null; pos+=mlen;
    var payload=[];
    for(var i=0;i<payLen;i++) payload.push(masked?buf[pos+i]^mask[i%4]:buf[pos+i]);
    return {opcode:opcode,fin:fin,payload:payload,consumed:pos+payLen-offset};
}

// ── Per-connection state (keyed by fd)

var _wsConns = {};
function _wsInit(fd)  { _wsConns[fd] = {buf:[], pending:''}; }
function _wsFree(fd)  { delete _wsConns[fd]; }

// ── Shared helpers

function _push(fd, obj) {
    nginx.repl._writeFdRaw(fd, wsTextFrame(JSON.stringify(obj)));
}

function _state() {
    return {
        type:    'state',
        header:  nginx.shared.get('demo.header')  || 'none',
        version: parseInt(nginx.shared.get('demo.version') || '0', 10),
        worker:  String(nginx.workerIdx)
    };
}

// ── WebSocket message handler

function _onWsMessage(fd, text) {
    var msg;
    try { msg = JSON.parse(text); } catch(e) { return; }

    if (msg.type === 'poll') {
        _push(fd, _state());
        return;
    }

    if (msg.type === 'set') {
        var val = String(msg.value || '').trim();
        if (!val) return;
        nginx.shared.set('demo.header', val);
        nginx.shared.set('demo.version',
            String(parseInt(nginx.shared.get('demo.version') || '0', 10) + 1));
        _push(fd, _state());
        return;
    }
}

// ── WebSocket raw-data handler (called per TCP segment)

function _onWsData(fd, chunk) {
    var conn = _wsConns[fd];
    if (!conn) return;
    for(var i=0;i<chunk.length;i++) conn.buf.push(chunk[i]);
    var offset=0;
    for(;;){
        var frame=parseWsFrame(conn.buf, offset);
        if(!frame) break;
        offset+=frame.consumed;
        if(frame.opcode===8){
            nginx.repl._writeFdRaw(fd, wsCloseFrame(1000));
            _wsFree(fd);
            return;
        }
        if(frame.opcode===9){
            nginx.repl._writeFdRaw(fd, new Uint8Array([0x8A,0]));
            continue;
        }
        if(frame.opcode===1||frame.opcode===0){
            var part='';
            for(var i=0;i<frame.payload.length;i++) part+=String.fromCharCode(frame.payload[i]);
            conn.pending+=part;
            if(frame.fin){ _onWsMessage(fd, conn.pending); conn.pending=''; }
        }
    }
    if(offset>0) conn.buf=conn.buf.slice(offset);
}

// ── HTTP → WebSocket upgrade handler

function _wsUpgrade(req) {
    var upgrade = req.headers['upgrade'];
    var wsKey   = req.headers['sec-websocket-key'];
    var connHdr = req.headers['connection'] || '';
    if (!upgrade || upgrade.toLowerCase()!=='websocket' ||
        !wsKey   || connHdr.toLowerCase().indexOf('upgrade')===-1) {
        req.respond(400, {}, 'WebSocket upgrade required\n');
        return;
    }
    var fd = req.hijack();
    nginx.repl._writeFd(fd,
        'HTTP/1.1 101 Switching Protocols\r\n' +
        'Upgrade: websocket\r\n' +
        'Connection: Upgrade\r\n' +
        'Sec-WebSocket-Accept: ' + wsAccept(wsKey) + '\r\n\r\n');
    _wsInit(fd);
    nginx.repl.listenRaw(fd, function(chunk){ _onWsData(fd, chunk); });
}

// ── Browser UI (inline HTML — no separate static/ directory needed)

var _UI = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Live Header Demo · nginx JS COM</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Menlo,Monaco,Consolas,monospace;background:#0d1117;color:#e6edf3;padding:2rem}
h1{font-size:.9rem;color:#7d8590;font-weight:normal;margin-bottom:1.5rem;letter-spacing:.05em;text-transform:uppercase}
.part-label{font-size:.7rem;color:#484f58;text-transform:uppercase;letter-spacing:.12em;margin-bottom:.75rem;
            padding:.3rem .6rem;border:1px solid #21262d;border-radius:4px;display:inline-block}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:1.25rem 1.75rem;margin-bottom:1.25rem}
.section-title{font-size:.75rem;color:#7d8590;text-transform:uppercase;letter-spacing:.1em;margin-bottom:.75rem}
.label{font-size:.65rem;color:#7d8590;text-transform:uppercase;letter-spacing:.08em;margin-bottom:.35rem}
#hdr-val{font-size:2.4rem;font-weight:bold;color:#3fb950;transition:color .2s}
#hdr-val.flash{color:#ffa657}
.controls{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center;margin-bottom:1rem}
input[type=text]{background:#0d1117;border:1px solid #30363d;color:#e6edf3;padding:.4rem .65rem;
      border-radius:6px;font-family:inherit;font-size:.85rem;width:160px}
input[type=checkbox]{width:15px;height:15px;cursor:pointer;vertical-align:middle}
button{background:#21262d;border:1px solid #30363d;color:#e6edf3;padding:.4rem .65rem;
       border-radius:6px;font-family:inherit;font-size:.8rem;cursor:pointer}
button:hover{background:#30363d}
.btn-primary{background:#1f6feb;border-color:#1f6feb;color:#fff}
.btn-primary:hover{background:#388bfd}
.btn-danger{background:#6e1a1a;border-color:#8b2222;color:#ffa0a0}
.btn-danger:hover{background:#8b2222}
.log-hdr{background:#21262d;padding:.45rem 1rem;border-bottom:1px solid #30363d;
         font-size:.65rem;color:#7d8590;text-transform:uppercase;letter-spacing:.1em}
#log{padding:.65rem 1rem;font-size:.78rem;line-height:1.8;max-height:180px;overflow-y:auto}
.ts{color:#484f58} .val{color:#3fb950;font-weight:bold} .wk{color:#58a6ff}
.bar{font-size:.7rem;color:#484f58;margin-bottom:.75rem;display:flex;gap:1rem}
#ws-dot.on{color:#3fb950} #ws-dot.off{color:#f85149}
hr.sep{border:none;border-top:1px solid #21262d;margin:1.5rem 0}
.hdr-table{width:100%;border-collapse:collapse;font-size:.8rem;margin-top:.5rem}
.hdr-table th{text-align:left;color:#7d8590;font-weight:normal;padding:.3rem .5rem;border-bottom:1px solid #21262d}
.hdr-table td{padding:.35rem .5rem;border-bottom:1px solid #161b22;vertical-align:middle}
.hdr-table tr:last-child td{border-bottom:none}
.pill-always{background:#1a3a1a;color:#3fb950;border-radius:3px;padding:.1rem .35rem;font-size:.7rem}
.pill-skip{background:#2a1a1a;color:#8b3333;border-radius:3px;padding:.1rem .35rem;font-size:.7rem}
.rm-btn{background:none;border:1px solid #8b2222;color:#f85149;padding:.15rem .4rem;border-radius:4px;font-size:.7rem;cursor:pointer}
.rm-btn:hover{background:#8b2222;color:#fff}
.response-box{background:#0d1117;border:1px solid #21262d;border-radius:6px;padding:.65rem 1rem;
              font-size:.78rem;line-height:1.7;max-height:180px;overflow-y:auto;margin-top:.5rem}
.response-box .rh-key{color:#58a6ff} .response-box .rh-val{color:#e6edf3}
.response-box .rh-injected{color:#3fb950;font-weight:bold}
.response-box .rh-status{color:#7d8590}
.empty-note{color:#484f58;font-style:italic;padding:.4rem 0}
.code-hint{background:#0d1117;border:1px solid #21262d;border-radius:4px;padding:.3rem .6rem;
           font-size:.75rem;color:#7d8590;margin-top:.4rem;display:inline-block}
</style>
</head>
<body>
<h1>nginx JS COM &middot; A2.8 &mdash; Live Header Mutation &mdash; Two Approaches</h1>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- PART 1 — Request-phase header via nginx.shared + WebSocket             -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->

<span class="part-label">Part 1 &mdash; Request-phase header (nginx.shared + WebSocket)</span>
<div class="card">
  <div class="label">X-Api-Header &mdash; live value across all 4 workers</div>
  <div id="hdr-val">__INITIAL_HEADER__</div>
</div>

<div class="controls">
  <input id="inp" type="text" value="v2-premium" placeholder="new value">
  <button class="btn-primary" onclick="doSet()">Set Header</button>
  <button onclick="preset('v1-initial')">v1-initial</button>
  <button onclick="preset('v2-premium')">v2-premium</button>
  <button onclick="preset('v3-enterprise')">v3-enterprise</button>
</div>

<div class="bar">
  <span><span id="ws-dot">&#9679;</span> <span id="ws-lbl">connecting</span></span>
  <span id="wk-lbl"></span>
</div>

<div class="card" style="padding:0;overflow:hidden">
  <div class="log-hdr">Change log (Part 1)</div>
  <div id="log"><div style="color:#484f58;padding:.5rem .75rem">No changes yet.</div></div>
</div>

<div class="code-hint">curl -X POST "http://127.0.0.1:8117/admin/set-header/?value=v2-premium"</div>

<hr class="sep">

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- PART 2 — Config-phase header via location.headers.addHeader() API      -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->

<span class="part-label">Part 2 &mdash; Config-phase header (loc.headers.addHeader API)</span>

<div class="card">
  <div class="section-title">
    Location: <span style="color:#e6edf3">/plain/</span>
    &nbsp;&nbsp;<span style="color:#484f58">|</span>&nbsp;&nbsp;
    Config: <span style="color:#ffa657">return 200 "nginx plain response";</span>
    &nbsp;&nbsp;<span style="color:#484f58">|</span>&nbsp;&nbsp;
    <span style="color:#f85149">No JS handler</span>
  </div>

  <div class="label" style="margin-top:.75rem">Current add_header entries on /plain/</div>
  <table class="hdr-table" id="cfg-table">
    <thead><tr><th>Key</th><th>Value</th><th>Always</th><th></th></tr></thead>
    <tbody id="cfg-body"><tr><td colspan="4" class="empty-note">&nbsp;&nbsp;(loading…)</td></tr></tbody>
  </table>

  <div style="margin-top:1rem">
    <div class="controls" style="margin-bottom:.5rem">
      <input id="cfg-key"   type="text" placeholder="Header key" style="width:140px">
      <input id="cfg-val"   type="text" placeholder="Header value" style="width:140px">
      <label style="font-size:.8rem;color:#7d8590">
        <input id="cfg-always" type="checkbox"> always
      </label>
      <button class="btn-primary" onclick="cfgInject()">Inject Header</button>
      <button class="btn-danger"  onclick="cfgClear()">Clear All</button>
    </div>
    <div class="code-hint" id="inject-hint">curl -X POST "/admin/add-config-header/?key=X-Feature&amp;value=beta"</div>
  </div>
</div>

<div class="card">
  <div style="display:flex;align-items:center;gap:.75rem;margin-bottom:.5rem">
    <div class="label" style="margin:0">Live response headers from <span style="color:#e6edf3">GET /plain/</span></div>
    <button onclick="fetchPlain()" style="font-size:.75rem">&#8635; Refresh</button>
  </div>
  <div class="response-box" id="plain-hdrs"><span class="empty-note">click Refresh to fetch</span></div>
  <div class="code-hint" style="margin-top:.5rem">curl -I http://127.0.0.1:8117/plain/</div>
</div>

<script>
// ── Part 1: WebSocket for nginx.shared header ──────────────────────────────
var ws, last = null, timer = null;
function conn(){
  ws = new WebSocket('ws://'+location.host+'/ws/');
  ws.onopen = function(){
    setStatus(true);
    poll();
    timer = setInterval(poll, 300);
  };
  ws.onmessage = function(e){
    var m; try{m=JSON.parse(e.data);}catch(ex){return;}
    if(m.type!=='state') return;
    document.getElementById('hdr-val').textContent = m.header;
    document.getElementById('wk-lbl').textContent  = 'worker '+m.worker+' · v'+m.version;
    if(m.header!==last){
      var el=document.getElementById('hdr-val');
      el.classList.add('flash');
      setTimeout(function(){el.classList.remove('flash');},500);
      logEntry(m.header, m.worker);
      last=m.header;
    }
  };
  ws.onclose = function(){ setStatus(false); clearInterval(timer); setTimeout(conn,1500); };
  ws.onerror = function(){ ws.close(); };
}
function setStatus(up){
  var d=document.getElementById('ws-dot'), l=document.getElementById('ws-lbl');
  d.className=up?'on':'off'; l.textContent=up?'connected':'disconnected (reconnecting…)';
}
function poll(){ if(ws&&ws.readyState===1) ws.send(JSON.stringify({type:'poll'})); }
function doSet(){
  var v=document.getElementById('inp').value.trim();
  if(v&&ws&&ws.readyState===1) ws.send(JSON.stringify({type:'set',value:v}));
}
function preset(v){ document.getElementById('inp').value=v; doSet(); }
function logEntry(hdr, wk){
  var el=document.getElementById('log');
  if(el.firstChild&&el.firstChild.style&&el.firstChild.style.color) el.innerHTML='';
  var d=document.createElement('div');
  var ts=new Date().toTimeString().slice(0,12);
  d.innerHTML='<span class="ts">'+ts+'</span>  <span class="val">'+esc(hdr)+'</span>  <span class="wk">worker '+esc(String(wk))+'</span>';
  el.insertBefore(d,el.firstChild);
}
function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
conn();

// ── Part 2: config-phase header mutation ──────────────────────────────────
function cfgRefreshTable(){
  fetch('/admin/get-config-headers/').then(function(r){return r.json();}).then(function(d){
    var hdrs = d.headers || [];
    var tbody = document.getElementById('cfg-body');
    if(hdrs.length===0){
      tbody.innerHTML='<tr><td colspan="4" class="empty-note">&nbsp;&nbsp;(no add_header entries — /plain/ responds with no custom headers)</td></tr>';
      return;
    }
    tbody.innerHTML = hdrs.map(function(h,i){
      return '<tr>'+
        '<td style="color:#e6edf3">'+esc(h.key)+'</td>'+
        '<td style="color:#3fb950">'+esc(h.value||'(complex)')+'</td>'+
        '<td>'+(h.always?'<span class="pill-always">always</span>':'<span class="pill-skip">skip on error</span>')+'</td>'+
        '<td><button class="rm-btn" onclick="cfgRemove(this.dataset.key)" data-key="'+esc(h.key)+'">remove</button></td>'+
        '</tr>';
    }).join('');
  }).catch(function(){
    document.getElementById('cfg-body').innerHTML='<tr><td colspan="4" style="color:#f85149">fetch failed</td></tr>';
  });
}

function cfgInject(){
  var k=document.getElementById('cfg-key').value.trim();
  var v=document.getElementById('cfg-val').value.trim();
  var a=document.getElementById('cfg-always').checked;
  if(!k||!v){ alert('Key and value required'); return; }
  var url='/admin/add-config-header/?key='+encodeURIComponent(k)+'&value='+encodeURIComponent(v)+(a?'&always=1':'');
  document.getElementById('inject-hint').textContent='curl -X POST "http://127.0.0.1:8117'+url+'"';
  fetch(url,{method:'POST'}).then(function(r){return r.json();}).then(function(){
    cfgRefreshTable();
    fetchPlain();
  });
}

function cfgRemove(key){
  fetch('/admin/remove-config-header/?key='+encodeURIComponent(key),{method:'POST'})
    .then(function(){cfgRefreshTable();fetchPlain();});
}

function cfgClear(){
  fetch('/admin/clear-config-headers/',{method:'POST'})
    .then(function(){cfgRefreshTable();fetchPlain();});
}

function fetchPlain(){
  fetch('/plain/').then(function(r){
    var injected=['x-feature','x-security','x-injected','x-always','x-never','x-extra','x-config'];
    var lines=['<span class="rh-status">HTTP/1.1 '+r.status+' '+r.statusText+'</span>'];
    r.headers.forEach(function(val,name){
      var isInj = name.toLowerCase().startsWith('x-');
      lines.push((isInj?'<span class="rh-injected">':'<span class="rh-key">')+
                 esc(name)+'</span>: <span class="rh-val">'+esc(val)+'</span>');
    });
    document.getElementById('plain-hdrs').innerHTML=lines.join('<br>');
  }).catch(function(e){
    document.getElementById('plain-hdrs').innerHTML='<span style="color:#f85149">fetch failed: '+esc(String(e))+'</span>';
  });
}

// Auto-load table and response on page open
cfgRefreshTable();
fetchPlain();
</script>
</body>
</html>`;

// ── Install all handlers in every worker ─────────────────────────────────────
//
// nginx.broadcast(fn) runs fn once in every worker during init_process
// (after fork, before the first request is accepted).  Handlers installed
// here are private to each worker's JS context but all read the same
// nginx.shared region, so a write in worker 2 is immediately visible in
// worker 3's next nginx.shared.get() call.

nginx.broadcast(function () {

    // Seed initial values — first worker wins, others skip.
    if (nginx.shared.get('demo.header') === undefined) {
        nginx.shared.set('demo.header',  'v1-initial');
        nginx.shared.set('demo.version', '0');
    }

    var locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        var loc = locs.find(function(l){ return l.path === path; });
        if (loc) loc.handler = fn;
    }

    // ── GET / — browser UI ──────────────────────────────────────────────────
    // Inject the current header value directly into the HTML so the browser
    // displays it immediately on load — no WebSocket round-trip required.
    set('/', function(req) {
        var initialHeader = nginx.shared.get('demo.header') || 'v1-initial';
        req.respond(200, {'Content-Type': 'text/html'},
            _UI.replace('__INITIAL_HEADER__', initialHeader));
    });

    // ── GET /ws/ — WebSocket endpoint ──────────────────────────────────────
    //
    // Client sends {"type":"poll"} every 300 ms  → server replies with current
    //               state: {type:"state", header, version, worker}.
    // Client sends {"type":"set","value":"..."} → server updates nginx.shared
    //               and replies with the new state immediately.
    //
    // The connection stays alive; the client never reconnects just because
    // the header changed.
    set('/ws/', _wsUpgrade);

    // ── POST /admin/set-header/?value=<val> — change the header ────────────
    //
    // Writes to nginx.shared — a single mmap region shared by all worker
    // processes.  The write is visible to all other workers before this
    // response even leaves the kernel.  No reload, no signal, no IPC.
    set('/admin/set-header/', function(req) {
        var val = ((req.queryParams && req.queryParams.value) || '').trim();
        if (!val) {
            req.respond(400, {}, 'usage: POST /admin/set-header/?value=<new-value>\n');
            return;
        }
        nginx.shared.set('demo.header', val);
        nginx.shared.set('demo.version',
            String(parseInt(nginx.shared.get('demo.version') || '0', 10) + 1));
        req.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({
                ok:      true,
                header:  val,
                version: parseInt(nginx.shared.get('demo.version'), 10),
                worker:  req.variable('pid')
            }) + '\n');
    });

    // ── GET /api/ — application endpoint that reflects the live header ──────
    //
    // Returns X-Api-Header in the response headers AND in the JSON body so
    // both curl -I and curl show the current value.
    set('/api/', function(req) {
        var hdr = nginx.shared.get('demo.header') || 'none';
        req.respond(200, {
            'Content-Type':  'application/json',
            'X-Api-Header':  hdr
        }, JSON.stringify({
            ok:             true,
            'X-Api-Header': hdr,
            worker:         req.variable('pid')
        }) + '\n');
    });

    // ── GET /status/ — inspect shared state ────────────────────────────────
    set('/status/', function(req) {
        req.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({
                header:  nginx.shared.get('demo.header')  || 'none',
                version: parseInt(nginx.shared.get('demo.version') || '0', 10),
                worker:  req.variable('pid')
            }) + '\n');
    });

    // ── Part 2: config-phase add_header mutation on /plain/ ─────────────────
    //
    // /plain/ has NO JS content handler — nginx responds via the rewrite
    // module (`return 200`).  The header filter applies loc.headers.addHeaders[]
    // to every response with no JS involvement in request handling.
    //
    // Cross-worker propagation uses cfgbus.js — a SharedWorker broadcast bus:
    //
    //   admin endpoint           cfgbus.js (SW thread)       other workers
    //   ─────────────────        ─────────────────────        ──────────────
    //   addHeader locally   →   update canonical state   →   addHeader locally
    //   postMessage(msg)    →   fan-out to other ports   →   onmessage fires
    //
    // The sender applies the change locally (immediate); cfgbus fans the same
    // message to every other worker so they apply it in their next event-loop
    // iteration — typically well before the next HTTP request arrives.

    var plainLoc = locs.find(function(l){ return l.path === '/plain/'; });

    // One SharedWorker per nginx instance (thread in the master process).
    // Each worker gets its own port connection.
    var cfgBus = new SharedWorker(nginx.cycle.prefix + 'cfgbus.js');

    // Receive fan-out messages from other workers via the SW.
    cfgBus.onmessage = function(e) {
        var msg = e.data;

        if (msg.type === 'setState') {
            // Catch-up on (re)connect: apply the SW's canonical state.
            plainLoc.headers.addHeaders = [];
            for (var i = 0; i < msg.headers.length; i++) {
                var h = msg.headers[i];
                plainLoc.headers.addHeader(h.key, h.value, h.always);
            }

        } else if (msg.type === 'addHeader') {
            plainLoc.headers.addHeader(msg.key, msg.value, msg.always);

        } else if (msg.type === 'removeHeader') {
            plainLoc.headers.removeHeader(msg.key);

        } else if (msg.type === 'clearHeaders') {
            plainLoc.headers.addHeaders = [];
        }
    };

    // POST /admin/add-config-header/?key=K&value=V[&always=1]
    set('/admin/add-config-header/', function(req) {
        var key    = ((req.queryParams && req.queryParams.key)   || '').trim();
        var val    = ((req.queryParams && req.queryParams.value) || '').trim();
        var always = (req.queryParams && req.queryParams.always) === '1';
        if (!key || !val) {
            req.respond(400, {}, 'usage: ?key=K&value=V[&always=1]\n');
            return;
        }
        // Apply locally first — immediate for /plain/ on THIS worker.
        plainLoc.headers.addHeader(key, val, always);
        // Fan out to all other workers via the broadcast bus.
        cfgBus.postMessage({type: 'addHeader', key: key, value: val, always: always});
        req.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({ok: true, action: 'addHeader',
                            key: key, value: val, always: always}) + '\n');
    });

    // POST /admin/remove-config-header/?key=K
    set('/admin/remove-config-header/', function(req) {
        var key = ((req.queryParams && req.queryParams.key) || '').trim();
        if (!key) {
            req.respond(400, {}, 'usage: ?key=K\n');
            return;
        }
        plainLoc.headers.removeHeader(key);
        cfgBus.postMessage({type: 'removeHeader', key: key});
        req.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({ok: true, action: 'removeHeader', key: key}) + '\n');
    });

    // POST /admin/clear-config-headers/
    set('/admin/clear-config-headers/', function(req) {
        plainLoc.headers.addHeaders = [];
        cfgBus.postMessage({type: 'clearHeaders'});
        req.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({ok: true, action: 'clearHeaders'}) + '\n');
    });

    // GET /admin/get-config-headers/  — read current list + worker PID
    set('/admin/get-config-headers/', function(req) {
        req.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({
                headers: plainLoc.headers.addHeaders,
                worker:  req.variable('pid')
            }) + '\n');
    });
});
