#!/usr/bin/perl

# COMCON V15 / SR-4 — the assurance case is checked, not asserted.
#
# js_comcon/docs-v5.0/ASSURANCE.md is the claim -> assumption -> evidence tree that
# VERIFICATION.md asks for and that ROADMAP's gate table calls SR-4. Its entire value is
# that its leaves point at things that EXIST: a tree nobody checks decays into marketing
# at exactly the rate the code moves.
#
# That is not hypothetical here. Building it swept the doc set for cited test files and
# found 20 of 83 citations pointing at files deleted during the CONVERGENCE -- a reviewer
# following THREATS.md's T11 citation to t/comcon_gas.t found nothing at all. The rename
# table in ASSURANCE.md §12 is the redirect, and check-assurance.py is what stops the next
# twenty.
#
# This file runs the checker, so the case breaks the SUITE rather than waiting for someone
# to re-read it. It asserts every check RAN (an early exit that prints a clean summary is
# the vacuous pass this project keeps meeting) and that no drift was found.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

plan(tests => 9);

my $tool = "tools/check-assurance.py";
ok(-f $tool, "the assurance checker exists ($tool)");

my $out = `python3 $tool 2>&1`;
my $rc  = $?;

like($out, qr/\[1\] evidence artifacts exist/,
     'check 1 ran (every EV names an artifact that is there)');
like($out, qr/\[2\] every leaf carries evidence/,
     'check 2 ran (no claim without evidence or an owned gap)');
like($out, qr/\[3\] no orphan evidence/,
     'check 3 ran (every comcon test belongs to a claim)');
like($out, qr/\[4\] coverage/,
     'check 4 ran (every adversary and every V-item appears)');
like($out, qr/\[5\] no document cites a test file that does not exist/,
     'check 5 ran (the doc set has no dangling citations) -- this is the check '
     . 'that found F1, and the one that keeps finding it');
like($out, qr/\[6\] every finding named by a gap exists/,
     'check 6 ran (a gap cannot point at a finding nobody wrote down)');

is($rc, 0, "no assurance drift\n" . $out);

# The findings ledger must not be empty: an assurance case that claims everything is
# evidenced has not been built honestly, it has been written to reassure.
my $case = do { local (@ARGV, $/) = ('../js_comcon/docs-v5.0/ASSURANCE.md'); <> };
my @findings = ($case =~ /^\|\s*\*\*(F\d+)\*\*/gm);
cmp_ok(scalar @findings, '>=', 5,
       'the findings ledger names at least five open items -- a case with an '
       . 'empty ledger is marketing, and the gap detector has been switched off');
