#!/usr/bin/perl

# COMCON V7 — enumerations are generated, never maintained (VERIFICATION.md).
#
# Five enumerations in this design are CLOSED and their COMPLETENESS is
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
#   intrinsics          the C3 admission allowance: the engine's list vs the V3
#                       oracle's model of it (and Date/Math must stay OUT)
#   denial codes        MANUAL §3.2 promises tenants the codes are stable and
#                       tells them to pin CI to codes -- so every code in the C
#                       enum must carry a V12 golden-corpus row, with a probe or
#                       a written reason it is unreachable
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

plan(tests => 6);

my $tool = "tools/check-enumerations.py";
ok(-f $tool, "the enumeration checker exists ($tool)");

my $out = `python3 $tool 2>&1`;
my $rc  = $?;

# The checker must have actually run EVERY check -- "no drift" printed by a
# checker that bailed out early is the vacuous pass this suite keeps meeting.
# The last one matters most here: a new check appended to the tool is exactly
# what an early exit would swallow.
like($out, qr/\[1\] p_symbol kinds/,  'check 1 ran (p_symbol kinds)');
like($out, qr/\[2\] compile portals/, 'check 2 ran (compile portals)');
like($out, qr/\[4\] C3 intrinsics/,   'check 4 ran (C3 intrinsics allowance)');
like($out, qr/\[5\] denial codes/,    'check 5 ran (denial codes vs the V12 corpus)');

is($rc, 0, "no enumeration drift\n" . $out);
