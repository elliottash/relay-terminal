<!-- Board Plan brief v3 (#WC3E; v2 #K3TY, protocol 19.10; v1 #XS6Q). Sent at the head of
     every Plan turn on a card, after the card itself on the first one. `{card}` is the card's id
     and `{plan_heading}` the section it writes (`Plan`; SWITCHBOARD-DESIGN 12.4: plan mode writes
     the plan onto the card). v3 adds `## Done means`, the other section a Plan turn may write. -->

This is a **Plan** turn on #{card}: write the plan for this card, or revise the one it has, into
the card's `## {plan_heading}` section, and the expectations into its `## Done means`. The plan is
what a terminal-pane agent will be handed when the owner presses **Execute**, so write it for that
reader.

1. **Read first.** `board_read` #{card} for its current text and `hash`. Then read the code the card
   is about: `search_files` to find it, `read_file` and `list_directory` to read it. Cards named in
   `links.plans` or `links.related` are context — read them with `board_read`. Do not guess at file
   names you have not looked at.
2. **Write `## Done means` first**, with one `board_update_card` call:
   `replace_section: {heading: "Done means", text: …}`. Two to five lines, no more: what the card
   is for, stated as the outcome someone could check, and how failure would be recognised — the
   symptom that would say it did not work. Write it before the plan, from the issue and the
   owner's decisions, not from the steps you are about to choose: it is what a *different* session
   verifies the work against, and expectations picked after the fact only ever pass. A card that
   already has one is revised, not replaced, unless the issue has moved under it.
3. **Write the plan** with a second `board_update_card` call: `id: {card}`, `base_hash` from the
   result of the last write, and `replace_section: {heading: "{plan_heading}", text: …}`, the text
   without the heading line. If the card already has a plan, revise it rather than starting over,
   and keep what still holds. On `board_conflict`, read again and reapply.
4. **What a plan holds**, in Markdown, short and exact:
   - **Goal** — one or two sentences, in terms of the card's issue and its `acceptance` if it has one.
   - **Findings** — what the code does today, with exact paths (and functions).
   - **Steps** — numbered, each one change a reviewer can check.
   - **Orchestration** — only when the work is big enough to split across subagents: each subagent
     (its type and a one-line task), which steps run in parallel, and which wait for which. Only
     steps that touch no shared files may run in parallel; writes stay with the main agent that
     Execute hands the card to. A small plan gets no block.
   - **Risks** — what could break, and anything the owner has to decide (as a question).
   - **Verify** — the tests to run or add, and how to see it working.
5. **Nothing else changes.** You cannot run commands, write files, or change another card, and
   the tools will refuse if you try; this card's title, issue, labels and status are not yours to
   change in a Plan turn either — `## Done means` and `## {plan_heading}` are the two sections you
   may write, and the tools refuse any other. An open question for the owner goes in the plan's
   Risks, or as a `board_comment` of kind `question` on #{card}.
6. **Reply** in two or three sentences: what the plan does and what it needs from the owner, if
   anything. The plan itself is on the card; do not repeat it.
