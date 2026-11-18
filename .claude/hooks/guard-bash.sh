#!/usr/bin/env bash
# PreToolUse (Bash): block commands that bypass the workflow gates or
# destroy work. Exit 2 = block, stderr goes to Claude.

cmd=$(jq -r '.tool_input.command // empty')
[ -z "$cmd" ] && exit 0

block() { echo "Blocked: $1" >&2; exit 2; }

grep -Eq -- '--no-verify|git[[:space:]]+commit([[:space:]]+-[a-zA-Z]*)*[[:space:]]+-[a-zA-Z]*n[a-zA-Z]*([[:space:]]|$)' <<<"$cmd" \
  && block "skipping git hooks (--no-verify) bypasses the quality gate."
grep -Eq 'git[[:space:]]+push.*(--force|[[:space:]]-f([[:space:]]|$))' <<<"$cmd" \
  && block "force-push is not allowed."
grep -Eq 'git[[:space:]]+(reset[[:space:]]+--hard|clean[[:space:]]+-[a-zA-Z]*f|checkout[[:space:]]+--[[:space:]]+\.)' <<<"$cmd" \
  && block "this discards uncommitted work; ask the owner first."
grep -Eq 'core\.hooksPath' <<<"$cmd" && ! grep -Eq 'core\.hooksPath[[:space:]]+\.githooks' <<<"$cmd" \
  && block "changing core.hooksPath would disable the commit gate."
grep -Eq 'rm[[:space:]]+(-[a-zA-Z]*[rf][a-zA-Z]*[[:space:]]+)+.*(data|reference|plugin/Source|main\.py|\.git([[:space:]]|/|$))' <<<"$cmd" \
  && block "recursive delete of project sources/data; ask the owner first."

# No direct commits on main once it has history (phase branches only).
if grep -Eq 'git[[:space:]]+commit' <<<"$cmd"; then
  cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || exit 0
  branch=$(git symbolic-ref --short HEAD 2>/dev/null)
  if { [ "$branch" = "main" ] || [ "$branch" = "master" ]; } && git rev-parse --verify -q HEAD >/dev/null; then
    block "commits go on a phase branch (git switch -c phase-N-<slug>), not on $branch."
  fi
fi
exit 0
