#!/usr/bin/perl

# The dead-probe sweep, run as a test so it cannot rot unnoticed.
#
# V11 mutation-tests the POLICIES.  Nothing tested the TESTS -- and this arc
# found five assertions that could not fail, every one of them by accident while
# doing something else:
#
#   * the `realize` escape probe forged a 4th argument to a 3-argument operator,
#     so the forgery was discarded and the probe passed on every build;
#   * the `cap.expired` golden-corpus row reported "alive" forever, and nothing
#     pinned the row, so the suite stayed green;
#   * the `Symbol.for` cross-identity arm read `Symbol.for(k) === Symbol.for(k)`
#     -- two calls in ONE fragment -- which cannot come out 'clean';
#   * that same file's coverage tally scanned the whole payload, so a battery of
#     seven reported 8-of-8 once an unrelated arm was added;
#   * the assurance ledger's F9 row counted five unbuilt V-items where the
#     placement table has always shown six.
#
# A dead probe does not fail.  It reassures.  That is why this is a gate and not
# a report: the four shapes above are cheap to detect statically and were each
# expensive to find by hand.
#
# WHAT A CLEAN RUN DOES NOT MEAN, since the whole point is not to over-credit an
# instrument: this is a static reader.  It cannot tell whether an assertion's
# subject is reachable, whether a control fires, or whether a corpus row is
# compared against its expectation at run time.  Only running a mutation answers
# that -- `t/tools/verify-negative-controls.sh` does it for named fixes, and the
# comcon_* suites carry their controls inline.  Clean here means "no assertion is
# dead in one of the four ways this tree has already been burned by".

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

plan(tests => 6);

my $tool = "tools/check-dead-probes.py";
ok(-f $tool, "the dead-probe checker exists ($tool)");

my $out = `python3 $tool 2>&1`;
my $rc  = $?;

# Every check must be shown to have RUN.  "No findings" printed by a checker
# that bailed out early is the vacuous pass this file exists to prevent -- the
# same argument t/comcon_enumerations.t makes about its own tool, and the reason
# check [7] and check [3] were both found missing from that list.
like($out, qr/\[1\] a DISCRIMINATOR/, 'check 1 ran (self-comparison discriminators)');
like($out, qr/\[2\] assertions true by construction/, 'check 2 ran (tautologies)');
like($out, qr/\[3\] corpora/, 'check 3 ran (corpus readers and scraper drift)');
like($out, qr/\[4\] a coverage tally/, 'check 4 ran (payload-wide tallies)');

is($rc, 0, "no dead probes\n" . $out);
