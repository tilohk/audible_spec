#!/usr/bin/env bash
# PostToolUse (Edit|Write): format the edited file and run the RT-safety lint.
# Exit 2 = report a problem back to Claude (the edit itself already happened).

file=$(jq -r '.tool_response.filePath // .tool_input.file_path // empty')
[ -z "$file" ] || [ ! -f "$file" ] && exit 0
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
hooks="$root/.claude/hooks"

case "$file" in
  *.cpp|*.h|*.hpp|*.mm)
    if command -v clang-format >/dev/null 2>&1; then
      clang-format -i --style=file "$file"
    else
      echo "note: clang-format not installed (brew install clang-format); C++ formatting skipped." >&2
    fi
    if ! "$hooks/rt-lint.sh" "$file"; then
      exit 2
    fi
    ;;
  *.py)
    ruff="$root/venv/bin/ruff"
    command -v "$ruff" >/dev/null 2>&1 || ruff=$(command -v ruff)
    if [ -n "$ruff" ]; then
      "$ruff" format -q "$file"
      if ! "$ruff" check -q "$file" >&2; then
        echo "ruff found problems in $file" >&2
        exit 2
      fi
    fi
    ;;
esac
exit 0
