<!-- relay:entry 20260926T130222Z-v3 author=agent kind=event model=gpt-6-sol pane=91b2ca54 turn=c3e05da713f34109bf1893fed2425e6f/d7aaacb12f0645a48189c0decd7de88f -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-26-board-metadata-conflicts-fail-reconciliation-whe.md

<!-- relay:entry 20260926T130243Z-07 author=agent kind=event model=gpt-6-sol pane=91b2ca54 turn=c3e05da713f34109bf1893fed2425e6f/d7aaacb12f0645a48189c0decd7de88f -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260926T130248Z-3g author=agent kind=evidence model=gpt-6-sol pane=91b2ca54 turn=c3e05da713f34109bf1893fed2425e6f/d7aaacb12f0645a48189c0decd7de88f -->
Observed queue metadata jobs `3c9fd1e8ffbb43e5` (`guest:codex:ashe-ethz-ch`) and `7879feb36cc50b96` (`guest:claude`): each reached status `conflict` with reason `WorkspacePreparationError: queue development requires a session token`; both have `workspace_id: null`, no published SHA, and were submitted as Board metadata after the target moved. Related code jobs #736Y/#4TNY also failed the accepted full gate, separately tracked by #BP15. The large-file and binary refusals in their reconciliation paths are explicit conservative guards in `backend/relay_core/reconcile.py`.

<!-- relay:entry 20260926T130248Z-mp author=agent kind=event model=gpt-6-sol pane=91b2ca54 turn=c3e05da713f34109bf1893fed2425e6f/d7aaacb12f0645a48189c0decd7de88f -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260926T130331Z-cx author=agent kind=event model=gpt-6-sol pane=91b2ca54 turn=c3e05da713f34109bf1893fed2425e6f/d7aaacb12f0645a48189c0decd7de88f -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260926T130334Z-ev author=agent kind=event model=gpt-6-sol pane=91b2ca54 turn=c3e05da713f34109bf1893fed2425e6f/d7aaacb12f0645a48189c0decd7de88f -->
- ✦ agent moved this card · Discussing → Planned · Failure is reproduced by two metadata jobs, acceptance and focused repair plan are recorded, and no owner product decision remains.
