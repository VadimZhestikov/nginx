package ComStress;

# Helpers for COM steady-state leak tests.
# Each stress endpoint runs N COM iterations in one request and returns:
#   "before=NNN after=NNN delta=NNN" (all in bytes, JS malloc_size)

use strict;
use warnings;
use Test::More;

our @EXPORT = qw(run_stress assert_flat);
use Exporter 'import';

# run_stress($t, $path, $n) — GET path?n=$n, parse response, return hashref
# {before, after, delta} (all in bytes).
sub run_stress {
    my ($t, $path, $n) = @_;
    my $r = main::http_get("$path?n=$n");
    my ($body) = ($r =~ /\r\n\r\n(.*)/s);
    my %d;
    $d{$1} = $2+0 while $body =~ /(\w+)=(\d+)/g;
    return \%d;
}

# assert_flat($d, $n, $label, [$threshold_bytes]) — fail if delta exceeds
# threshold.  Default 32768 bytes (32 KB): generous for GC timing noise,
# tight enough to catch a leaked wrapper per iteration at n=1000.
sub assert_flat {
    my ($d, $n, $label, $thresh) = @_;
    $thresh //= 32768;
    my $delta = $d->{delta} // $d->{after} - $d->{before};
    cmp_ok($delta, '<', $thresh,
           "$label: JS heap delta ${delta}B < ${thresh}B over $n iters");
}

1;
