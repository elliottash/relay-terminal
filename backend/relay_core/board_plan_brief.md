<!-- Switchboard Plan brief v1 (#XS6Q, protocol 19.10). Sent at the head of every Plan turn on
     a card, after the card itself on the first one. `{card}` is the card's id and
     `{plan_heading}` the section it writes (`Plan`; SWITCHBOARD-DESIGN 12.4: plan mode writes
     the plan onto the card). -->

This is a **Plan** turn on #{card}: write the plan for this card, or revise the one it has, into
the card's `## {plan_heading}` section. The plan is what a terminal-pane agent will be handed when
the owner presses **Execute**, so write it for that reader.

1. **Read first.** `board_read` #{card} for its current text and `hash`. Then read the code the card
   is about: `search_files` to find it, `read_file` and `list_directory` to read it. Cards named in
   `links.plans` or `links.related` are context — read them with `board_read`. Do not guess at file
   names you have not looked at.
2. **Write** with one `board_update_card` call: `id: {card}`, `base_hash` from `board_read`, and
   `replace_section: {heading: "{plan_heading}", text: …}`, the text without the heading line. If
   the card already has a plan, revise it rather than starting over, and keep what still holds. On
   `board_conflict`, read again and reapply.
3. **What a plan holds**, in Markdown, short and exact:
   - **Goal** — one or two sentences, in terms of the card's issue and its `acceptance` if it has one.
   - **Findings** — what the code does today, with exact paths (and functions).
   - **Steps** — numbered, each one change a reviewer can check.
   - **Risks** — what could break, and anything the owner has to decide (as a question).
   - **Verify** — the tests to run or add, and how to see it working.
4. **Nothing else changes.** You cannot run commands, write files, or change another card, and
   the tools will refuse if you try; this card's title, issue, labels and status are not yours to
   change in a Plan turn either. An open question for the owner goes in the plan's Risks, or as a
   `board_comment` of kind `question` on #{card}.
5. **Reply** in two or three sentences: what the plan does and what it needs from the owner, if
   anything. The plan itself is on the card; do not repeat it.
