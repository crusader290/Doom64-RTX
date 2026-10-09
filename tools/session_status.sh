#!/usr/bin/env bash
# tools/session_status.sh - tiny status for the start of a Claude session (SessionStart hook).
# Prints at most 3 lines; never fails the session.
cd "$(dirname "$0")/.." 2>/dev/null || exit 0
tools/setup_env.sh --check 2>/dev/null | head -1
echo "git: $(git rev-parse --abbrev-ref HEAD 2>/dev/null) @ $(git rev-parse --short HEAD 2>/dev/null)$(git status --porcelain 2>/dev/null | grep -q . && echo ' (uncommitted changes)')"
python3 - <<'PY' 2>/dev/null
import json
t = json.load(open('docs/tasks.json'))['tasks']
act = [x['id'] for x in t if x['status'] in ('in_progress', 'blocked')]
pend = [x['id'] for x in t if x['status'] == 'pending']
print("tasks: active " + (",".join(act) or "-") + "; pending " + (",".join(pend) or "-") + " (docs/tasks.json; read PROJECT_CONTEXT.md first)")
PY
exit 0
