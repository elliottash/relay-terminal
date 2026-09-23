<!-- relay:entry 20260922T184119Z-rc author=agent kind=evidence model=gpt-6-astra pane=eda7f821 turn=678e5e19bfda407591a623f02e9421f6/3d4068e3f6b64f3c866e4bb7d09805ca -->
Filed measured failures discovered during SPB2. No application or test source changed during the build. Full package gate is not green; development artifact delivery will state that explicitly.

<!-- relay:entry 20260922T184119Z-rd author=agent kind=event model=gpt-6-astra pane=eda7f821 turn=678e5e19bfda407591a623f02e9421f6/3d4068e3f6b64f3c866e4bb7d09805ca -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260922T184330Z-ge author=agent kind=event model=gpt-6-astra pane=eda7f821 turn=678e5e19bfda407591a623f02e9421f6/3d4068e3f6b64f3c866e4bb7d09805ca -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-sphinxpad… → {"plans": [], "commits": ["f589f8c57bb5efdefa4b4bdbceb6bf064b874ebf"], "evidence…

<!-- relay:entry 20260923T211618Z-8q author=agent kind=note model=claude-opus-5-5 pane=6b0b0195 turn=d20c4203bb934061b37f73dc69f56a6d/6445f3f6e3cf43be8539a62e7066ec2d -->
A second `boardworkspace` failure at `a532eaf`: `aCardTurnsEventsReachThatCardsConsoleAndNoOther` asserts that the `deliverToConsoles` source contains `entry.context->spec().surface != card`, and it no longer does. #G4VB's run earlier today showed only the `openOutputTarget` failure, so this one is newer. Seen while testing #6W9X, which does not touch that code.
