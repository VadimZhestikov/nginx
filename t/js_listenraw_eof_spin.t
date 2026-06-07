#!/usr/bin/perl

# Regression test for the listenRaw EOF spin:
#   After req.hijack() + nginx.repl.listenRaw(fd, cb), when the remote peer
#   closes the TCP connection WITHOUT sending a WebSocket close frame, nginx
#   received EOF (recvmsg / recv returns 0) in ngx_js_repl_read_handler.
#   The handler jumped to cleanup: which freed state but did NOT call
#   ngx_del_event().  The hijacked fd remained registered in level-triggered
#   epoll, causing the worker to spin at ~100% CPU indefinitely.
#
# Fix: ngx_js_repl_read_handler cleanup: now calls
#   ngx_del_event(c->read, NGX_READ_EVENT, 0) before ngx_http_finalize_request.
#
# Verification:
#   1. Open a WebSocket connection (HTTP 101 Upgrade).
#   2. Send one poll message and receive the response.
#   3. Close the raw TCP socket WITHOUT sending a WS close frame (plain EOF).
#   4. Wait 3 seconds.
#   5. Assert that all worker processes are idle (CPU < 20%).
#   6. Assert that the nginx instance is still serving new requests correctly.

use warnings;
use strict;
use IO::Socket::INET;
use MIME::Base64 qw(encode_base64);
use Digest::SHA  qw(sha1);
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(6);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/listenraw_eof.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /ws/     { }
        location /probe/  { }
    }
}
EOF

$t->write_file('listenraw_eof.js', <<'JS');
/* ── SHA-1 and base64 needed for the WebSocket handshake ── */
function sha1(bytes) {
    var H=[0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0];
    function u32(n){return n>>>0;}
    function rol(n,s){return u32((n<<s)|(n>>>(32-s)));}
    function add(a,b){return u32(a+b);}
    var msg=bytes.slice(),len=msg.length;
    msg.push(0x80);
    while((msg.length%64)!==56)msg.push(0);
    var bits=len*8;
    for(var i=7;i>=0;i--)msg.push(Math.floor(bits/Math.pow(2,i*8))&0xFF);
    for(var blk=0;blk<msg.length;blk+=64){
        var W=[];
        for(var j=0;j<16;j++)W[j]=(msg[blk+j*4]<<24)|(msg[blk+j*4+1]<<16)|(msg[blk+j*4+2]<<8)|msg[blk+j*4+3];
        for(var j=16;j<80;j++)W[j]=rol(u32(W[j-3]^W[j-8]^W[j-14]^W[j-16]),1);
        var a=H[0],b=H[1],c=H[2],d=H[3],e=H[4];
        for(var j=0;j<80;j++){
            var f,k;
            if(j<20){f=u32((b&c)|((~b)&d));k=0x5A827999;}
            else if(j<40){f=u32(b^c^d);k=0x6ED9EBA1;}
            else if(j<60){f=u32((b&c)|(b&d)|(c&d));k=0x8F1BBCDC;}
            else{f=u32(b^c^d);k=0xCA62C1D6;}
            var tmp=add(add(add(add(rol(a,5),f),e),u32(k)),u32(W[j]));
            e=d;d=c;c=rol(b,30);b=a;a=tmp;
        }
        H[0]=add(H[0],a);H[1]=add(H[1],b);H[2]=add(H[2],c);H[3]=add(H[3],d);H[4]=add(H[4],e);
    }
    var out=[];
    for(var i=0;i<5;i++)out.push((H[i]>>>24)&0xFF,(H[i]>>>16)&0xFF,(H[i]>>>8)&0xFF,H[i]&0xFF);
    return out;
}
var _B64='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function base64enc(bytes){
    var out='';
    for(var i=0;i<bytes.length;i+=3){
        var n=(bytes[i]<<16)|((bytes[i+1]||0)<<8)|(bytes[i+2]||0);
        out+=_B64[(n>>18)&63]+_B64[(n>>12)&63];
        out+=(i+1<bytes.length)?_B64[(n>>6)&63]:'=';
        out+=(i+2<bytes.length)?_B64[n&63]:'=';
    }
    return out;
}
function wsAccept(key){
    var src=key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11',bytes=[];
    for(var i=0;i<src.length;i++)bytes.push(src.charCodeAt(i));
    return base64enc(sha1(bytes));
}
function wsTextFrame(text){
    var p=[];
    for(var i=0;i<text.length;i++){
        var c=text.charCodeAt(i);
        if(c<0x80)p.push(c);
        else if(c<0x800)p.push(0xC0|(c>>6),0x80|(c&0x3F));
        else p.push(0xE0|(c>>12),0x80|((c>>6)&0x3F),0x80|(c&0x3F));
    }
    var h=[0x81];
    if(p.length<126)h.push(p.length);
    else h.push(126,(p.length>>8)&0xFF,p.length&0xFF);
    return new Uint8Array(h.concat(p));
}
function parseWsFrame(buf,off){
    if(buf.length-off<2)return null;
    var b0=buf[off],b1=buf[off+1],masked=(b1&0x80)!==0,payLen=b1&0x7F,pos=off+2;
    if(payLen===126){if(buf.length-pos<2)return null;payLen=(buf[pos]<<8)|buf[pos+1];pos+=2;}
    var mLen=masked?4:0;
    if(buf.length-pos<mLen+payLen)return null;
    var mask=masked?buf.slice(pos,pos+4):null;pos+=mLen;
    var pay=[];for(var i=0;i<payLen;i++)pay.push(masked?buf[pos+i]^mask[i%4]:buf[pos+i]);
    return{opcode:b0&0x0F,fin:(b0&0x80)!==0,payload:pay,consumed:pos+payLen-off};
}

var _wsConns={};
function _wsInit(fd){_wsConns[fd]={buf:[],pending:''};}
function _wsFree(fd){delete _wsConns[fd];}

function _onWsMsg(fd,text){
    var msg;try{msg=JSON.parse(text);}catch(e){return;}
    if(msg.type==='poll'){
        nginx.repl._writeFdRaw(fd,wsTextFrame(JSON.stringify({type:'state',ok:true})));
    }
}

function _onWsData(fd,chunk){
    var conn=_wsConns[fd];if(!conn)return;
    for(var i=0;i<chunk.length;i++)conn.buf.push(chunk[i]);
    var off=0;
    for(;;){
        var frame=parseWsFrame(conn.buf,off);if(!frame)break;
        off+=frame.consumed;
        if(frame.opcode===8){nginx.repl._writeFdRaw(fd,new Uint8Array([0x88,0]));_wsFree(fd);return;}
        if(frame.opcode===1||frame.opcode===0){
            var part='';for(var i=0;i<frame.payload.length;i++)part+=String.fromCharCode(frame.payload[i]);
            conn.pending+=part;
            if(frame.fin){_onWsMsg(fd,conn.pending);conn.pending='';}
        }
    }
    if(off>0)conn.buf=conn.buf.slice(off);
}

nginx.broadcast(function(){
    var locs=nginx.http.servers[0].locations;
    function set(p,fn){var l=locs.find(function(l){return l.path===p;});if(l)l.handler=fn;}

    /* WebSocket upgrade endpoint */
    set('/ws/',function(req){
        var key=req.headers['sec-websocket-key'];
        var conn=req.headers['connection']||'';
        if(!key||conn.toLowerCase().indexOf('upgrade')===-1){req.respond(400,{},'bad\n');return;}
        var fd=req.hijack();
        nginx.repl._writeFd(fd,
            'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+
            wsAccept(key)+'\r\n\r\n');
        _wsInit(fd);
        nginx.repl.listenRaw(fd,function(chunk){_onWsData(fd,chunk);});
    });

    /* Simple probe endpoint to verify the worker is still functional */
    set('/probe/',function(req){req.respond(200,{},'alive\n');});
});
JS

$t->run();

# ── 1. WebSocket handshake ────────────────────────────────────────────────

my $s = IO::Socket::INET->new(
    PeerAddr => '127.0.0.1',
    PeerPort => 8080,
    Proto    => 'tcp',
) or die "cannot connect: $!";

my $raw_key = 'listenraweof0001';
my $key     = encode_base64($raw_key, '');
my $accept  = encode_base64(sha1($key . '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'), '');
chomp $accept;

print $s "GET /ws/ HTTP/1.1\r\nHost: localhost\r\n" .
         "Upgrade: websocket\r\nConnection: Upgrade\r\n" .
         "Sec-WebSocket-Key: $key\r\nSec-WebSocket-Version: 13\r\n\r\n";

my $resp = '';
while ($resp !~ /\r\n\r\n/) {
    sysread($s, my $buf, 4096) or last;
    $resp .= $buf;
}
like($resp, qr{HTTP/1\.1 101},                           'WS: 101 Switching Protocols');
like($resp, qr{Sec-WebSocket-Accept: \Q$accept\E},       'WS: correct Accept header');

# ── 2. Send one poll frame and receive response ───────────────────────────

my $poll_payload = '{"type":"poll"}';
my $mask         = "\x01\x02\x03\x04";
my $masked_pay   = $poll_payload ^ ($mask x 4);
substr($masked_pay, length($poll_payload)) = '';
print $s pack('CC', 0x81, 0x80 | length($poll_payload)) . $mask . $masked_pay;

my $recv = '';
eval {
    local $SIG{ALRM} = sub { die "timeout\n" };
    alarm 3;
    sysread($s, $recv, 4096);
    alarm 0;
};
ok(length($recv) > 0, 'WS: received response frame after poll');

# ── 3. Close TCP WITHOUT sending WS close frame — the EOF trigger ─────────

close $s;   # plain TCP close, no WS close frame — this was the bug trigger

# ── 4. Wait 3 seconds then check no worker is spinning ───────────────────

sleep 3;

my $master_pid = $t->read_file('nginx.pid');
chomp $master_pid;

my $max_cpu = 0;
if (open my $fh, '-|', 'ps', '--ppid', $master_pid,
                             '-o', 'pcpu', '--no-header')
{
    while (<$fh>) {
        chomp(my $cpu = $_);
        $cpu =~ s/^\s+//;
        $max_cpu = $cpu if $cpu > $max_cpu;
    }
    close $fh;
}

ok($max_cpu < 20,
   "no worker CPU spin after WS EOF (max ${max_cpu}%)");

# ── 5. Worker still handles new requests after the EOF ───────────────────

my $probe = http_get('/probe/');
like($probe, qr{HTTP/1\.1 200}, 'worker still functional after WS EOF');
like($probe, qr{alive},         'probe body correct');
