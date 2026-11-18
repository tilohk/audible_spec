#!/usr/bin/env bash
# Shared quality gate: build + tests + lint. Used by the Stop hook and
# by .githooks/pre-commit. Prints failures to stderr, exit 1 on failure.
# Skips stages whose prerequisites don't exist yet (early phases).

root="${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
cd "$root" || exit 1
fail=0
log=$(mktemp)

# 1. RT-safety lint over all plugin sources.
if [ -d plugin/Source ]; then
  files=$(find plugin/Source -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \))
  if [ -n "$files" ] && ! .claude/hooks/rt-lint.sh $files; then fail=1; fi
fi

# 2. C++ build + tests (only once the build dir has been configured).
if [ -f plugin/build/CMakeCache.txt ]; then
  if ! cmake --build plugin/build -j >"$log" 2>&1; then
    echo "BUILD FAILED (last 40 lines):" >&2; tail -40 "$log" >&2; fail=1
  elif [ -f plugin/build/CTestTestfile.cmake ] || find plugin/build -maxdepth 2 -name CTestTestfile.cmake | grep -q .; then
    if ! ctest --test-dir plugin/build --output-on-failure >"$log" 2>&1; then
      echo "TESTS FAILED (last 60 lines):" >&2; tail -60 "$log" >&2; fail=1
    fi
  fi
fi

# 3. Python reference tests (once they exist).
if [ -d reference/tests ] && [ -x venv/bin/python ]; then
  if ! venv/bin/python -m pytest -q reference/tests >"$log" 2>&1; then
    echo "PYTHON TESTS FAILED:" >&2; tail -40 "$log" >&2; fail=1
  fi
fi

rm -f "$log"
exit $fail
