<!-- relay:entry 20260921T021312Z-hx author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-potential-bug-switchboard-agent-didnt-recognize.md

<!-- relay:entry 20260922T042712Z-vz author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260922T042712Z-w0 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260922T043134Z-24 author=agent kind=event model=kimi-k3 pane=switchboard turn=acbd1b222ea7d90317a55b525eb41b2c/d2a49c7e44134d15bfb54564eba9d110 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260922T043213Z-25 author=agent kind=event model=kimi-k3 pane=switchboard turn=acbd1b222ea7d90317a55b525eb41b2c/d2a49c7e44134d15bfb54564eba9d110 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T043223Z-nt author=agent kind=comment mode=plan model=kimi-k3 turn=acbd1b222ea7d90317a55b525eb41b2c/d2a49c7e44134d15bfb54564eba9d110 -->
This is a single planning task, so no todos. Let me read the code around session-id handling in the app tools.

Let me look at how the conversation index resolves a session id, since that's the lookup the agent depends on.





The file is too large to read whole. Let me probe the `conversation()` lookup specifically.



















**Plan written on #XHH4.** The gap is real: `app_sessions_search` is pure full-text over titles and messages, a bare session id never matches, and the only id-exact lookup (`app_open` → conversation) opens a pane as a side effect — so an agent handed an id from the ⓘ info has no way to recognize it. The plan adds an id/prefix lookup in `conv_index` wired into `_sessions_search`, with tests and a live Switchboard check.

One caveat for Execute: step 1 confirms from the 2026-09-20 logs that the pasted id was a conversation id — if it was actually a pane token, the fix belongs elsewhere and the executing agent should re-scope rather than force this one.

<!-- relay:entry 20260922T043223Z-xn author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
