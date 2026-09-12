#!/usr/bin/perl

# COMCON V7 — enumerations are generated, never maintained (VERIFICATION.md).
#
# Three enumerations in this design are CLOSED and their COMPLETENESS is
# load-bearing:
#
#   p_symbol kinds      a kind that means two things at two tiers is a wrong
#                       answer that looks right (it happened: a
#                       FunctionDeclaration reported stmt at the CST tier and
#                       function at the bytecode tier, so query('function')
#                       silently found nothing at one of them)
#   compile portals     "no hidden compile path" -- only checkable if the list of
#                       places src/js turns text into code is complete
#   ops resources       FOUNDATION §8a's "no backdoor" -- the verbs are library
#                       code over exactly these capabilities
#
# Hand-maintained lists rot, and the rot is SILENT: nothing fails when a list
# stops matching the code. So t/tools/check-enumerations.py derives each list from
# the source and fails on drift, and this file runs it, so drift breaks the suite
# instead of waiting for someone to re-read a document. (VERIFICATION.md says
# "drift = build failure"; a standing test is this project's spelling of that.)
#
# It found real drift on its first run: `mode` -- the audit/enforce/learn switch
# that std.ops' rollout verbs decompose over -- was in the code and absent from
# §8a's list of seven resources.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

plan(tests => 4);

my $tool = "tools/check-enumerations.py";
ok(-f $tool, "the enumeration checker exists ($tool)");

my $out = `python3 $tool 2>&1`;
my $rc  = $?;

# The checker must have actually run all three checks -- "no drift" printed by a
# checker that bailed out early is the vacuous pass this suite keeps meeting.
like($out, qr/\[1\] p_symbol kinds/,  'check 1 ran (p_symbol kinds)');
like($out, qr/\[2\] compile portals/, 'check 2 ran (compile portals)');

is($rc, 0, "no enumeration drift\n" . $out);
