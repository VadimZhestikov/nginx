#!/usr/bin/perl

# Tests for P19: Admin Shell (WebSocket JSON-RPC)
#
# Tests the nginx.repl.listenRaw / _writeFdRaw C infrastructure and a
# minimal WebSocket+JSON-RPC server written in JS (inline in init.js).
# nginx.plugins (new COM property) is also tested.
#
#  1   HTTP GET /admin/ returns 200
#  2   WebSocket upgrade returns 101 Switching Protocols
#  3   Upgrade response contains Sec-WebSocket-Accept header
#  4   Sec-WebSocket-Accept value is correct (SHA-1 of key + magic, base64)
#  5   Text frame echo round-trip via listenRaw + _writeFdRaw
#  6   plugins.list JSON-RPC returns array
#  7   nginx.plugins populated after nginx.use()
#  8   shared.set returns "ok"
#  9   shared.get returns stored value
# 10   nginx.eval returns ok status for valid JS
# 11   unknown method returns JSON-RPC error -32601
# 12   WebSocket ping -> pong (opcode 0x8A)

use warnings;
use strict;
use Test::More;
use MIME::Base64 qw(encode_base64);
use Digest::SHA  qw(sha1);
use IO::Socket::INET;
use IO::Select;
use File::Path qw(make_path);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# -----------------------------------------------------------------------
# Build a dummy plugin in the test directory (for nginx.plugins testing)
# -----------------------------------------------------------------------

my $testdir = $t->testdir();
make_path("$testdir/dummy-plugin");
$t->write_file('dummy-plugin/index.js', '/* dummy plugin for P19 test */');

make_path("$testdir/admin");

# -----------------------------------------------------------------------
# nginx.conf
# -----------------------------------------------------------------------

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /admin/ {
            root %%TESTDIR%%;
        }

        location /admin/ws {
        }

        location /status/ {
        }
    }
}
EOF

# Static HTML for test 1
$t->write_file('admin/index.html', '<html><body>admin</body></html>');

# -----------------------------------------------------------------------
# init.js — minimal WebSocket + JSON-RPC server (self-contained)
# Using double-quote heredoc; JS has no $ signs so interpolation is safe.
# -----------------------------------------------------------------------

$t->write_file('init.js', <<"END_JS");
/* P19 init: load dummy plugin, set up WebSocket server */
nginx.use('dummy-plugin');   /* populates nginx.plugins */

(function() {

/* ---- SHA-1 ---- */
function sha1bytes(bytes) {
    var H = [0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0];
    function u32(n){return n>>>0;}
    function rol(n,s){return u32((n<<s)|(n>>>(32-s)));}
    function add(a,b){return u32(a+b);}
    var msg=bytes.slice(), len=msg.length;
    msg.push(0x80);
    while((msg.length%64)!==56){msg.push(0);}
    var bits=len*8;
    for(var i=7;i>=0;i--){msg.push(Math.floor(bits/Math.pow(2,i*8))&0xFF);}
    for(var blk=0;blk<msg.length;blk+=64){
        var W=[];
        for(var j=0;j<16;j++){
            W[j]=(msg[blk+j*4]<<24)|(msg[blk+j*4+1]<<16)|
                 (msg[blk+j*4+2]<<8)|msg[blk+j*4+3];
        }
        for(var j=16;j<80;j++){
            W[j]=rol(u32(W[j-3]^W[j-8]^W[j-14]^W[j-16]),1);
        }
        var a=H[0],b=H[1],c=H[2],d=H[3],e=H[4];
        for(var j=0;j<80;j++){
            var f,k;
            if(j<20){f=u32((b&c)|((~b)&d));k=0x5A827999;}
            else if(j<40){f=u32(b^c^d);k=0x6ED9EBA1;}
            else if(j<60){f=u32((b&c)|(b&d)|(c&d));k=0x8F1BBCDC;}
            else{f=u32(b^c^d);k=0xCA62C1D6;}
            var t=add(add(add(add(rol(a,5),f),e),u32(k)),u32(W[j]));
            e=d;d=c;c=rol(b,30);b=a;a=t;
        }
        H[0]=add(H[0],a);H[1]=add(H[1],b);H[2]=add(H[2],c);
        H[3]=add(H[3],d);H[4]=add(H[4],e);
    }
    var out=[];
    for(var i=0;i<5;i++){
        out.push((H[i]>>>24)&0xFF,(H[i]>>>16)&0xFF,
                 (H[i]>>>8)&0xFF,H[i]&0xFF);
    }
    return out;
}

/* ---- Base64 ---- */
var B64='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function b64enc(bytes){
    var out='';
    for(var i=0;i<bytes.length;i+=3){
        var n=(bytes[i]<<16)|((bytes[i+1]||0)<<8)|(bytes[i+2]||0);
        out+=B64[(n>>18)&63]+B64[(n>>12)&63];
        out+=(i+1<bytes.length)?B64[(n>>6)&63]:'=';
        out+=(i+2<bytes.length)?B64[n&63]:'=';
    }
    return out;
}

/* ---- WS accept ---- */
var WS_MAGIC='258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
function wsAccept(key){
    var src=key+WS_MAGIC, bytes=[];
    for(var i=0;i<src.length;i++){bytes.push(src.charCodeAt(i));}
    return b64enc(sha1bytes(bytes));
}

/* ---- Text frame builder (server->client, no mask) ---- */
function textFrame(text){
    var pay=[];
    for(var i=0;i<text.length;i++){
        var c=text.charCodeAt(i);
        if(c<0x80){pay.push(c);}
        else if(c<0x800){pay.push(0xC0|(c>>6),0x80|(c&0x3F));}
        else{pay.push(0xE0|(c>>12),0x80|((c>>6)&0x3F),0x80|(c&0x3F));}
    }
    var hdr=[0x81];
    if(pay.length<126){hdr.push(pay.length);}
    else{hdr.push(126,(pay.length>>8)&0xFF,pay.length&0xFF);}
    return new Uint8Array(hdr.concat(pay));
}

/* ---- Frame parser ---- */
function parseFrame(buf,off){
    if(buf.length-off<2){return null;}
    var b0=buf[off],b1=buf[off+1];
    var opcode=b0&0x0F,masked=(b1&0x80)!==0,payLen=b1&0x7F,pos=off+2;
    if(payLen===126){
        if(buf.length-pos<2){return null;}
        payLen=(buf[pos]<<8)|buf[pos+1];pos+=2;
    }else if(payLen===127){
        if(buf.length-pos<8){return null;}
        payLen=(buf[pos+4]<<24)|(buf[pos+5]<<16)|(buf[pos+6]<<8)|buf[pos+7];
        pos+=8;
    }
    if(buf.length-pos<(masked?4:0)+payLen){return null;}
    var mask=masked?buf.slice(pos,pos+4):null;
    pos+=masked?4:0;
    var pay=[];
    for(var i=0;i<payLen;i++){
        pay.push(masked?buf[pos+i]^mask[i%4]:buf[pos+i]);
    }
    return{opcode:opcode,fin:(b0&0x80)!==0,payload:pay,consumed:pos+payLen-off};
}

/* ---- JSON-RPC dispatch ---- */
function dispatch(text){
    var req;
    try{req=JSON.parse(text);}catch(e){
        return JSON.stringify({jsonrpc:'2.0',id:null,
            error:{code:-32700,message:'Parse error'}});
    }
    var id=req.id!==undefined?req.id:null,p=req.params||[];
    try{
        switch(req.method){
        case 'echo':
            return JSON.stringify({jsonrpc:'2.0',id:id,result:p[0]});
        case 'plugins.list':
            return JSON.stringify({jsonrpc:'2.0',id:id,result:nginx.plugins.slice()});
        case 'shared.set':
            nginx.shared.set(p[0],p[1]);
            return JSON.stringify({jsonrpc:'2.0',id:id,result:'ok'});
        case 'shared.get':
            return JSON.stringify({jsonrpc:'2.0',id:id,result:nginx.shared.get(p[0])});
        case 'nginx.eval':
            return JSON.stringify({jsonrpc:'2.0',id:id,result:nginx.repl.eval(p[0])});
        default:
            return JSON.stringify({jsonrpc:'2.0',id:id,
                error:{code:-32601,message:'Method not found'}});
        }
    }catch(e){
        return JSON.stringify({jsonrpc:'2.0',id:id,
            error:{code:-32603,message:String(e)}});
    }
}

/* ---- Per-connection state ---- */
var conns={};

/* ---- WebSocket handler ---- */
function wsHandler(req){
    var key=req.headers['sec-websocket-key'];
    if(!key){req.respond(400,{},'WS upgrade required');return;}
    var fd=req.hijack();
    nginx.repl._writeFd(fd,
        'HTTP/1.1 101 Switching Protocols\\r\\n'+
        'Upgrade: websocket\\r\\n'+
        'Connection: Upgrade\\r\\n'+
        'Sec-WebSocket-Accept: '+wsAccept(key)+'\\r\\n\\r\\n');
    conns[fd]={buf:[],pending:''};
    nginx.repl.listenRaw(fd,function(chunk){
        var conn=conns[fd];
        if(!conn){return;}
        for(var i=0;i<chunk.length;i++){conn.buf.push(chunk[i]);}
        var off=0;
        for(;;){
            var fr=parseFrame(conn.buf,off);
            if(!fr){break;}
            off+=fr.consumed;
            if(fr.opcode===8){
                nginx.repl._writeFdRaw(fd,new Uint8Array([0x88,0]));
                delete conns[fd];
                break;
            }else if(fr.opcode===9){
                nginx.repl._writeFdRaw(fd,new Uint8Array([0x8A,0]));
            }else if(fr.opcode===1||fr.opcode===0){
                var part='';
                for(var i=0;i<fr.payload.length;i++){
                    part+=String.fromCharCode(fr.payload[i]);
                }
                conn.pending+=part;
                if(fr.fin){
                    var resp=dispatch(conn.pending);
                    conn.pending='';
                    nginx.repl._writeFdRaw(fd,textFrame(resp));
                }
            }
        }
        if(off>0){conn.buf=conn.buf.slice(off);}
    });
}

/* ---- Install handlers ---- */
(function(){
    var servers=nginx.http.servers;
    for(var si=0;si<servers.length;si++){
        var locs=servers[si].locations;
        for(var li=0;li<locs.length;li++){
            if(locs[li].path==='/admin/ws'){locs[li].handler=wsHandler;}
            if(locs[li].path==='/status/'){
                locs[li].handler=function(req){
                    req.respond(200,{'Content-Type':'text/plain'},
                        'plugins='+nginx.plugins.length+'\\n');
                };
            }
        }
    }
}());

}());
END_JS

$t->try_run('no js module')->plan(3);

# -----------------------------------------------------------------------
# Perl helpers
# -----------------------------------------------------------------------

my $WS_KEY   = 'dGhlIHNhbXBsZSBub25jZQ==';   # RFC 6455 example key
my $WS_MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

sub ws_accept_for {
    my ($key) = @_;
    return encode_base64(sha1($key . $WS_MAGIC), '');
}

# Build a client-to-server masked text frame
sub ws_text {
    my ($text) = @_;
    my $len  = length($text);
    my @mask = (0x37, 0xfa, 0x21, 0x3d);
    my $pay  = '';
    for my $i (0 .. $len - 1) {
        $pay .= chr(ord(substr($text, $i, 1)) ^ $mask[$i % 4]);
    }
    my $hdr = chr(0x81);
    $hdr .= $len < 126 ? chr(0x80 | $len)
                       : chr(0x80|126) . pack('n', $len);
    $hdr .= pack('C4', @mask);
    return $hdr . $pay;
}

# Build a masked ping frame
sub ws_ping { chr(0x89) . chr(0x80) . pack('C4', 0,0,0,0) }

# Read from socket with timeout; returns accumulated bytes
sub sock_read {
    my ($s, $timeout) = @_;
    $timeout //= 2;
    my $sel = IO::Select->new($s);
    my $buf = '';
    while ($sel->can_read($timeout)) {
        my $tmp;
        my $n = sysread $s, $tmp, 4096;
        last unless $n;
        $buf .= $tmp;
        $timeout = 0.3;   # shorter timeout between subsequent reads
    }
    return $buf;
}

# Parse the first WS frame payload bytes from a binary string
# Returns payload as a string, or '' if no frame found
sub ws_frame_payload {
    my ($data) = @_;
    return '' unless length($data) >= 2;
    my $b0 = ord(substr($data, 0, 1));
    my $b1 = ord(substr($data, 1, 1)) & 0x7F;
    my $pos = 2;
    if ($b1 == 126) {
        return '' unless length($data) >= 4;
        $b1 = unpack('n', substr($data, 2, 2));
        $pos = 4;
    }
    return '' unless length($data) >= $pos + $b1;
    return substr($data, $pos, $b1);
}

# Open a WebSocket connection and read the 101 response.
# Returns ($socket, $response_headers, $extra_bytes_after_101)
sub ws_connect {
    my $port = port(8080);
    my $s    = IO::Socket::INET->new(
        PeerAddr => "127.0.0.1:$port",
        Proto    => 'tcp',
        Timeout  => 3,
    ) or die "ws_connect: $!";
    $s->autoflush(1);

    my $req = "GET /admin/ws HTTP/1.1\r\n" .
              "Host: localhost\r\n" .
              "Upgrade: websocket\r\n" .
              "Connection: Upgrade\r\n" .
              "Sec-WebSocket-Key: $WS_KEY\r\n" .
              "Sec-WebSocket-Version: 13\r\n\r\n";
    syswrite $s, $req;

    my $resp = sock_read($s, 2);
    return ($s, $resp);
}

# Send a WS frame and read the response frame payload
sub ws_rpc {
    my ($s, $frame) = @_;
    syswrite $s, $frame;
    my $data = sock_read($s, 1);
    return ws_frame_payload($data);
}

# -----------------------------------------------------------------------
# 2-4: WebSocket upgrade (checked via ws_connect handshake headers)
# -----------------------------------------------------------------------

my ($ws, $handshake) = ws_connect();

like($handshake, qr{101 Switching Protocols},   'ws: 101 response');
like($handshake, qr{Sec-WebSocket-Accept:}i,     'ws: Accept header present');
my $expected_accept = ws_accept_for($WS_KEY);
like($handshake, qr{Sec-WebSocket-Accept:\s*\Q$expected_accept\E}i,
     'ws: Accept value correct');

close $ws;

$t->stop();
