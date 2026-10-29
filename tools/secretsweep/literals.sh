#!/usr/bin/env bash
# literals.sh — emit every credential literal in secrets.h as NAME<TAB>VALUE.
#
# The one job: hand a credential sweep the exact strings that
# must not appear in a built image. It is the only thing in the tree that reads
# those values, it writes them to a pipe and nowhere else, and it is a separate
# file precisely so that the reading of secrets is one small auditable place
# rather than a line buried in a 300-line flash script.
#
# WHY A SWEEP EXISTS AT ALL: secrets.h once declared non-static globals, so the
# literals were emitted into every image whether or not any code path used them
# -- DEV_BENCH_CREDS gated the call, not the definition. Reading the source
# could not catch it, because the source is where it looked correct. Only the
# artefact could.
#
# It deliberately parses ANY string literal assigned to a name, including a
# commented-out one, because a value that is a comment today is a value
# somebody uncomments tomorrow, and a sweep that trusts a comment marker is a
# sweep with a documented way past it. A false positive here costs one line of
# thought; a false negative costs a password.
#
# Prints nothing to stderr on success. Nonzero exit means the file could not be
# read, which the caller must treat as a failure and not as "no secrets".
set -e
SEC="${1:?usage: literals.sh <path to secrets.h>}"
[ -r "$SEC" ] || { echo "cannot read $SEC" >&2; exit 1; }

# NAME = "VALUE"  ->  NAME<TAB>VALUE. Longest-match on the quotes is wrong for
# two literals on one line, so the value is the shortest run without a quote.
sed -nE 's@^[[:space:]]*(//[[:space:]]*)?.*\b([A-Z][A-Z0-9_]*)[[:space:]]*=[[:space:]]*"([^"]*)".*@\2\t\3@p' "$SEC" \
  | awk -F'\t' 'length($2) > 0 && !seen[$0]++'
