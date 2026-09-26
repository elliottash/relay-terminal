<!-- relay:entry 20260925T221933Z-um author=agent kind=note -->
Filed by the models-verification sweep (revs 54018502 → 9a13cfe2, HEAD moved mid-sweep). Evidence: docs/qa_evidence/2026-09-25-verify-bug-cards/NOTES.md.

<!-- relay:entry 20260925T225818Z-3o author=agent kind=note -->
Models-verification sweep 2026-09-25, rev 2db96643 (clean worktree): a third instance of the same module-format drift — tests.test_pane_view.OutboxTests now errors in setUpClass: tests/outbox_peer.mjs does `import { Outbox, QUEUEABLE, pendingLine } from '../app/outbox.js'` and node rejects the named ESM import (app/outbox.js is CommonJS). The rest of test_pane_view runs; only this class is blocked. Same fix family as modelname.js.

