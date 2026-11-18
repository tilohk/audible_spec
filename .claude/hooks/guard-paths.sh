#!/usr/bin/env bash
# PreToolUse (Edit|Write|NotebookEdit): block edits to source assets,
# generated files and build output. Exit 2 = block, stderr goes to Claude.

input=$(cat)
file=$(printf '%s' "$input" | jq -r '.tool_input.file_path // .tool_input.notebook_path // empty')
[ -z "$file" ] && exit 0

root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
rel="${file#"$root"/}"

case "$rel" in
  data/images/*)
    echo "Blocked: data/images/ holds the owner's source photos; never modify them." >&2; exit 2 ;;
  reference/fixtures/*)
    echo "Blocked: fixtures are generated. Regenerate with 'venv/bin/python reference/export_fixtures.py' instead of hand-editing." >&2; exit 2 ;;
  venv/*|plugin/build/*|*/_deps/*)
    echo "Blocked: $rel is generated/third-party (venv, build output or fetched dependency)." >&2; exit 2 ;;
  .claude/hooks/*|.githooks/*)
    echo "Blocked: quality-gate hooks may only be changed after the owner explicitly approves the change in chat. Explain the proposed change instead." >&2; exit 2 ;;
esac
exit 0
