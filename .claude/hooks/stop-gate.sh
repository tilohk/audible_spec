#!/usr/bin/env bash
# Stop hook: before Claude ends a turn, run the quality gate if any source
# changed since the last green run. On failure, block the stop so Claude
# has to fix it (or explain it to the owner).

input=$(cat)
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
cd "$root" || exit 0
state=".claude/state"; mkdir -p "$state"
stamp="$state/last-green"

# Second stop attempt in a row: don't loop forever, but tell the owner.
if [ "$(printf '%s' "$input" | jq -r '.stop_hook_active // false')" = "true" ]; then
  echo '{"systemMessage":"Quality gate still failing. Claude stopped without a green build; see the reply for details."}'
  exit 0
fi

watched="plugin/Source plugin/Tests plugin/CMakeLists.txt reference main.py"
changed=""
for p in $watched; do
  [ -e "$p" ] || continue
  if [ -f "$stamp" ]; then
    c=$(find "$p" -type f -newer "$stamp" -not -path '*/fixtures/*' -not -name '*.pyc' 2>/dev/null | head -1)
  else
    c=$(find "$p" -type f 2>/dev/null | head -1)
  fi
  [ -n "$c" ] && { changed=1; break; }
done
[ -z "$changed" ] && exit 0

if out=$(.claude/hooks/check.sh 2>&1); then
  touch "$stamp"
  exit 0
fi

reason=$(printf 'Quality gate failed (build/tests/RT-lint). Fix it before finishing, or explain to the owner why it cannot be fixed now.\n\n%s' "$out")
jq -n --arg r "$reason" '{decision:"block", reason:$r}'
exit 0
