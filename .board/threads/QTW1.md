<!-- relay:entry 20260925T152601Z-9z author=agent kind=event model=claude-opus-5-5 pane=ba310466 turn=90b3725007bf4d71860a6b377e948a26/4db0c91a58294b67b3598df31d7b1810 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-models-pane-hides-no-key-on-ranked-rows-kimi-pay.md

<!-- relay:entry 20260925T152604Z-va author=agent kind=event model=claude-opus-5-5 pane=ba310466 turn=90b3725007bf4d71860a6b377e948a26/4db0c91a58294b67b3598df31d7b1810 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by anthropic/claude-opus-5-5 via claude-code, session ba310466

<!-- relay:entry 20260925T152604Z-vb author=agent kind=progress model=claude-opus-5-5 pane=ba310466 turn=90b3725007bf4d71860a6b377e948a26/4db0c91a58294b67b3598df31d7b1810 pane_token=ba310466-2a82-498c-9092-3df8ea0a4edc -->
Claimed (ba310466) · working on it from a terminal pane

Put the dead-row reason first in the via text and name the plan in the tooltip; offscreen test.

<!-- relay:entry 20260925T152841Z-sy author=agent kind=note model=claude-opus-5-5 pane=ba310466 turn=90b3725007bf4d71860a6b377e948a26/4db0c91a58294b67b3598df31d7b1810 -->
Diagnosis. `tier\main` rank 1 is `kimi|kimi-k3` (Moonshot pay-as-you-go, no key stored); the user's Kimi subscription is `kimi-code` (keyed, polled, 0% used) and is in no tier list. Routing is right to skip the keyless row: in 1,567 draws Kimi was never a candidate. The priorities page, rendered offscreen with the user's settings and key state (/tmp/kimiprobe/picker.png), showed the row as `kimi-k3 | kimi · pay-as-you-go · no …`: the reason was appended to the via text and elided, and both plans show the same model name `kimi-k3`. Fix: the reason goes first (`no key · kimi · pay-as-you-go`) and the tooltip names the plan.

<!-- relay:entry 20260925T153028Z-0v author=agent kind=event model=claude-opus-5-5 pane=ba310466 turn=90b3725007bf4d71860a6b377e948a26/4db0c91a58294b67b3598df31d7b1810 -->
- ✦ agent moved this card · Executing → Done, session ba310466 released · Landed 339d0f93: dead-row reason leads via; tooltip names plan; picker tests 62/62; after-render in evidence. · evidence docs/qa_evidence/2026-09-25-QTW1/ · verified_by anthropic/claude-opus-5-5 via claude-code
