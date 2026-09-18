<!-- relay:entry 20260918T002125Z-ew author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=0047b779 turn=f9365378a2ed407ca15c248c950f9018/5bd0d2c7fe0f4eb0b559c3923119da60 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-17-switchboard-task-checkboxes-are-read-only.md

<!-- relay:entry 20260918T002128Z-yd author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=0047b779 turn=f9365378a2ed407ca15c248c950f9018/5bd0d2c7fe0f4eb0b559c3923119da60 -->
- ✦ agent moved this card · Inbox → Discussing · Moved to discussing: needs the owner's answer on write behaviour before it can be implemented.

<!-- relay:entry 20260918T002141Z-2m author=agent kind=question model=deepseek/deepseek-v4.1-flash pane=0047b779 turn=f9365378a2ed407ca15c248c950f9018/5bd0d2c7fe0f4eb0b559c3923119da60 -->
1. Should ticking a task checkbox write the card file immediately (an instant append to the thread, no separate Save/confirm step), or should the tick be staged and only written on an explicit save?
Recommendation: write immediately. The card detail is already a direct editor for other fields, and instant writes keep the thread append-only and avoid a second source of truth (an unsaved UI state) that could silently diverge from the file.

<!-- relay:entry 20260918T002147Z-4r author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=0047b779 turn=f9365378a2ed407ca15c248c950f9018/5bd0d2c7fe0f4eb0b559c3923119da60 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260918T002217Z-sz author=owner kind=comment -->
Answer your own question in one sentence, then stop.

<!-- relay:entry 20260918T002235Z-rb author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=13de2c97f2fb464588bd356ce6ba2e2a/0ca1f28530df4398af35cacd84d03198 -->
- ✦ agent updated this card · waiting_on: owner → (unset)

<!-- relay:entry 20260918T002238Z-4z author=agent kind=decision model=deepseek/deepseek-v4.1-flash pane=switchboard turn=13de2c97f2fb464588bd356ce6ba2e2a/0ca1f28530df4398af35cacd84d03198 -->
Owner: "Answer your own question in one sentence, then stop."
Answer: ticking a task checkbox writes the card file immediately — an instant thread append, no separate save step — because the card detail already writes other fields directly and a staged tick would create an unsaved UI state that can diverge from the file.

<!-- relay:entry 20260918T002240Z-qh author=agent kind=comment model=deepseek/deepseek-v4.1-flash turn=13de2c97f2fb464588bd356ce6ba2e2a/0ca1f28530df4398af35cacd84d03198 -->
#WC2X — answered in one sentence: ticking a task checkbox writes the card file immediately (instant thread append, no save step); `waiting_on` cleared. Stopping here.
