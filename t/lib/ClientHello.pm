package ClientHello;

# Hand-built TLS ClientHello records.
#
# A TLS library will not send a malformed extension, which is exactly what this
# needs to send: nginx-js parses the INNER structure of server_name and ALPN
# itself (OpenSSL hands it the extension body and validates only the outer
# framing), so the bytes worth testing are the ones a real client never emits.
#
# build(%opts) returns a complete TLS record ready to write to a socket.
#   sni      => string        a well-formed server_name extension
#   sni_raw  => bytes         a server_name extension body, verbatim
#   alpn     => [protos]      a well-formed ALPN extension
#   alpn_raw => bytes         an ALPN extension body, verbatim
#
# The *_raw forms are the point: they place arbitrary bytes where the inner
# length fields go.

use strict;
use warnings;

sub u8  { return pack('C',  $_[0]); }
sub u16 { return pack('n',  $_[0]); }

# a length-prefixed block: u16 length followed by the payload
sub blk16 { return u16(length $_[0]) . $_[0]; }

sub ext {
    my ($type, $body) = @_;
    return u16($type) . blk16($body);
}

# server_name: list_len(2) type(1) name_len(2) name
sub sni_ext_body {
    my ($host) = @_;
    my $entry = u8(0) . blk16($host);
    return blk16($entry);
}

# ALPN: list_len(2) then [len(1) proto]*
sub alpn_ext_body {
    my (@protos) = @_;
    my $list = join '', map { u8(length $_) . $_ } @protos;
    return blk16($list);
}

sub build {
    my (%o) = @_;

    my @exts;
    if (defined $o{sni})      { push @exts, ext(0x0000, sni_ext_body($o{sni})); }
    if (defined $o{sni_raw})  { push @exts, ext(0x0000, $o{sni_raw}); }
    if (defined $o{alpn})     { push @exts, ext(0x0010, alpn_ext_body(@{ $o{alpn} })); }
    if (defined $o{alpn_raw}) { push @exts, ext(0x0010, $o{alpn_raw}); }

    # supported_versions (TLS 1.2) so OpenSSL is happy to proceed
    push @exts, ext(0x002b, u8(2) . u16(0x0303));
    # supported_groups: x25519, secp256r1
    push @exts, ext(0x000a, blk16(u16(0x001d) . u16(0x0017)));
    # ec_point_formats: uncompressed
    push @exts, ext(0x000b, u8(1) . u8(0));
    # signature_algorithms: rsa_pkcs1_sha256, ecdsa_secp256r1_sha256
    push @exts, ext(0x000d, blk16(u16(0x0401) . u16(0x0403)));

    my $extensions = blk16(join '', @exts);

    my $body = u16(0x0303)                      # client_version
             . ("\x41" x 32)                    # random
             . u8(0)                            # session_id (empty)
             . blk16(u16(0x009c) . u16(0x002f)) # cipher suites
             . u8(1) . u8(0)                    # compression: null
             . $extensions;

    # handshake header: type(1) + length(3)
    my $hs = u8(1) . substr(pack('N', length $body), 1, 3) . $body;

    # record header: type(1) version(2) length(2)
    return u8(0x16) . u16(0x0301) . blk16($hs);
}

# Send one ClientHello and drop the connection.  Returns 1 if the bytes went
# out; we do not care what comes back -- the callback has already run by then.
sub send_hello {
    my ($port, $record) = @_;
    my $s = IO::Socket::INET->new(Proto => 'tcp', PeerAddr => "127.0.0.1:$port",
                                  Timeout => 3);
    return 0 unless $s;
    local $SIG{PIPE} = 'IGNORE';
    print $s $record;
    $s->flush();
    # give nginx a moment to process before we tear the connection down
    select undef, undef, undef, 0.05;
    my $buf = '';
    eval {
        local $SIG{ALRM} = sub { die "timeout\n" };
        alarm 1;
        sysread($s, $buf, 16);
        alarm 0;
    };
    alarm 0;
    close $s;
    return 1;
}

1;
