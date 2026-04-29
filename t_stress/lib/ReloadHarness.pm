package ReloadHarness;

# Helpers for SIGHUP reload lifecycle leak tests.
#
# All measurements target the nginx MASTER process (its PID stays constant
# across reloads).  The master holds the JS runtime and SW socketpairs, so
# leaks in init/teardown show up in master RSS and master fd count.

use strict;
use warnings;
use Test::More;

our @EXPORT = qw(
    reload_nginx
    master_pid
    rss_kb
    fd_count
    assert_rss_stable
    assert_fd_stable
);
use Exporter 'import';


# master_pid($t) — read nginx master PID from Test::Nginx's pid file.
sub master_pid {
    my ($t) = @_;
    my $file = $t->testdir() . '/nginx.pid';
    open my $f, '<', $file or die "Cannot read $file: $!";
    my $pid = <$f>;
    chomp $pid;
    return $pid + 0;
}


# _child_pids($ppid) — return list of direct child PIDs in /proc.
sub _child_pids {
    my ($ppid) = @_;
    my @children;
    for my $proc (glob '/proc/[0-9]*') {
        open my $f, '<', "$proc/status" or next;
        my ($pid, $parent);
        while (my $line = <$f>) {
            $pid    = $1 if $line =~ /^Pid:\s+(\d+)/;
            $parent = $1 if $line =~ /^PPid:\s+(\d+)/;
        }
        push @children, $pid
            if defined $pid && defined $parent && $parent == $ppid;
    }
    return @children;
}


# reload_nginx($t) — SIGHUP the master, wait for old workers to exit.
# Returns master PID.
sub reload_nginx {
    my ($t, %opts) = @_;
    my $settle = $opts{settle} // 0.15;

    my $pid  = master_pid($t);
    my @old  = _child_pids($pid);

    kill 'HUP', $pid or die "SIGHUP $pid: $!";

    # Wait for every old worker to vanish from /proc (max 10 s).
    my $deadline = time() + 10;
    while (time() < $deadline) {
        my @alive = grep { -d "/proc/$_" } @old;
        last unless @alive;
        select undef, undef, undef, 0.05;
    }

    # Brief settle for new workers + JS init_process to complete.
    select undef, undef, undef, $settle;

    return $pid;
}


# rss_kb($pid) — VmRSS from /proc/$pid/status, in KB.
sub rss_kb {
    my ($pid) = @_;
    open my $f, '<', "/proc/$pid/status" or return -1;
    while (my $line = <$f>) {
        return $1 if $line =~ /^VmRSS:\s+(\d+)/;
    }
    return -1;
}


# fd_count($pid) — number of open file descriptors in /proc/$pid/fd/.
sub fd_count {
    my ($pid) = @_;
    opendir my $d, "/proc/$pid/fd" or return -1;
    return scalar(grep { $_ ne '.' && $_ ne '..' } readdir $d);
}


# assert_rss_stable($before, $after, $n, $label, [$threshold_kb])
# Passes if total RSS growth is below threshold.  Default 4 MB — generous
# enough to tolerate OS page-reclaim noise while catching leaked runtimes.
sub assert_rss_stable {
    my ($before, $after, $n, $label, $thresh) = @_;
    $thresh //= 4096;
    my $delta = $after - $before;
    cmp_ok($delta, '<', $thresh,
        "$label: RSS delta ${delta} KB < ${thresh} KB over $n reloads");
}


# assert_fd_stable($before, $after, $n, $label, [$slack])
# fd count is exact — any leaked socketpair/memfd shows up immediately.
# Default slack of 3 tolerates minor OS-level fd noise.
sub assert_fd_stable {
    my ($before, $after, $n, $label, $slack) = @_;
    $slack //= 3;
    my $delta = $after - $before;
    cmp_ok($delta, '<=', $slack,
        "$label: fd delta $delta <= $slack over $n reloads");
}

1;
