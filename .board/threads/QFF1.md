<!-- relay:entry 20260922T172507Z-yp author=agent kind=progress model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/7e55f54b457a4381aa202f3cb65d88b8 -->
Claimed via file fallback. Fixing empty Enter selecting the queue tail and ensuring the following Enter escalates the same prompt; explicit remote steering stays targeted.

<!-- relay:entry 20260922T172639Z-vk author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/7e55f54b457a4381aa202f3cb65d88b8 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T172705Z-6t author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/7e55f54b457a4381aa202f3cb65d88b8 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T172720Z-t3 author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/7e55f54b457a4381aa202f3cb65d88b8 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260922T172720Z-ty author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/7e55f54b457a4381aa202f3cb65d88b8 -->
- ✦ agent moved this card · Executing → Needs verification · Composer key-event regression and isolated Xvfb run pass; ready for independent verification. · evidence docs/qa_evidence/2026-09-22-enter-queue-order/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T172847Z-1r author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/7e55f54b457a4381aa202f3cb65d88b8 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-enter-que… → {"plans": [], "commits": ["124781856813f371cd066034ef4ad7bb90103be3"], "evidence…; appended to `## Execution Summary`

<!-- relay:entry 20260922T174349Z-2f author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T174525Z-34 author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T005730Z-7a author=agent kind=evidence model=gpt-6-astra pane=39ec8077 turn=d6aa0ecb8f9e4911b71ac7d61ec99bcb/f0abdca54ba8417895be3eb736687b76 -->
During #P7CK verification, the shared-checkout relay-consolemode-tests under Xvfb/xcb with isolated XDG_CONFIG_HOME fails repeatedEnterKeepsTheFirstQueuedPrompt at tests/consolemode_test.cpp:995 (sent.size() != 1). The new plan mouse-click assertions pass. Other sessions have uncommitted Pane.h edits; no queue edits made for P7CK.

<!-- relay:entry 20260923T025140Z-n8 author=agent kind=evidence -->
Check · 1 failed, 1 not-applicable; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T025221Z-71 author=agent kind=evidence model=gpt-6-astra pane=c95ef4b6 turn=a4d3237ded6e499298129ce091221313/946e3c14379a4e43babdf8659023bc67 -->
During #QSN1 validation, the existing consolemode repeatedEnterKeepsTheFirstQueuedPrompt case fails at tests/consolemode_test.cpp:996 (`sent.size() == 1`) before the new arrow test executes. New isolated --queue-arrow-only case passes. Investigating whether the broader failure depends on settings; no QFF1 code changed.

<!-- relay:entry 20260923T232103Z-wj author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T232342Z-19 author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260924T224257Z-8r author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260924T224908Z-hf author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260925T151459Z-k6 author=agent kind=evidence -->
Check · 1 failed, 1 not-applicable; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260925T151648Z-7k author=agent kind=evidence -->
Check · 1 failed, 1 not-applicable; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.
