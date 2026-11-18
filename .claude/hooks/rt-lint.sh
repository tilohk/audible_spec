#!/usr/bin/env bash
# Real-time safety lint.
# Scans code between "// RT-SAFE BEGIN" and "// RT-SAFE END" markers for
# patterns that must not run on the audio thread.
# Usage: rt-lint.sh file1.cpp [file2.h ...]
# Exit 0 = clean, 1 = violations or lint error (printed to stderr).
# Silence a single line with a trailing "// rt-ok: <reason>".

set -u
status=0

# Extended regex for grep -E. "[(]" is used instead of "\(" to keep escaping simple.
pattern='(^|[^A-Za-z_])(new[[:space:]]|delete[[:space:]]|malloc|calloc|realloc|free[(]|make_unique|make_shared|push_back|emplace_back|[.]resize[(]|[.]reserve[(]|std::mutex|lock_guard|unique_lock|scoped_lock|ScopedLock|CriticalSection|MessageManagerLock|sleep|[.]wait[(]|std::cout|std::cerr|printf|DBG[(]|Logger::|File[(]|juce::File|throw[[:space:]])'

for f in "$@"; do
  [ -f "$f" ] || continue
  # 1) Emit "lineno<TAB>code" for lines inside RT-SAFE regions, comments stripped,
  #    skipping lines annotated with "rt-ok:".
  region=$(awk '
    /\/\/[[:space:]]*RT-SAFE BEGIN/ { inside=1; next }
    /\/\/[[:space:]]*RT-SAFE END/   { inside=0; next }
    inside && $0 !~ /rt-ok:/ { code=$0; sub(/\/\/.*/, "", code); printf "%d\t%s\n", NR, code }
  ' "$f") || { echo "rt-lint: failed to parse $f" >&2; status=1; continue; }
  [ -z "$region" ] && continue

  # 2) Match forbidden patterns against the code part only.
  hits=$(printf '%s\n' "$region" | grep -E $'\t'".*$pattern")
  rc=$?
  if [ $rc -gt 1 ]; then echo "rt-lint: grep error on $f" >&2; status=1; continue; fi
  if [ -n "$hits" ]; then
    status=1
    printf '%s\n' "$hits" | sed "s|^\([0-9]*\)	|$f:\1: |" >&2
  fi
done

if [ $status -ne 0 ]; then
  echo "RT-safety lint failed: forbidden call inside an RT-SAFE region (see .claude/rules/realtime-audio.md)." >&2
fi
exit $status
