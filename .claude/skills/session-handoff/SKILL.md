---
name: session-handoff
description: Record verified progress and the next task before ending a session or after a milestone (PROJECT_CONTEXT.md, docs/tasks.json, docs/DISCOVERIES.md, PORT_MANIFEST.md).
---
# session-handoff

1. `docs/tasks.json`: set each touched task's `status` (pending / in_progress / blocked /
   implemented / verified). `verified` needs evidence you actually produced this session
   (check.sh OK + smoke PASS, screenshot, trace). Blocked/failed: fill `failure_class`
   (env | dependency | code | link | test | inconclusive), `last_approach`, `next`.
2. `PROJECT_CONTEXT.md`: rewrite the changed sections in place (milestone, verified list,
   build status with the commit, known problems, next task + acceptance criteria). Keep it
   under ~80 lines; never append session narratives.
3. New expensive-to-find facts → `docs/DISCOVERIES.md`. New/renamed files →
   `PORT_MANIFEST.md`. Optional milestone narrative → bottom of `docs/DEVLOG.md`.
4. `tools/check.sh linux` must be OK (or the failure recorded) before the final commit.
5. Commit and push; final report = changes, evidence, next step (a few lines).
