<!-- relay:entry 20260921T234209Z-a1 author=claude-code kind=comment -->
Filed from the owner's message; a scoping subagent is writing the plan. One trade-off for the owner's eye: `.switchboard/` was hidden on 2026-09-19 so cards stay out of a project's greps; `board/` is visible again.

<!-- relay:entry 20260922T001500Z-b1 author=claude-code kind=plan -->
### Scoping subagent · 2026-09-22 00:15
Wrote `## Plan` and moved the card to `planned`.

Counted: 222 live files and 1,944 occurrences of either spelling outside `.git/`, `issues/` and
`docs/qa_evidence/` — 1,147 capital `Switchboard`, 646 lowercase. `src/` 516, `tests/` 510,
`docs/` 460, `backend/` 324, the rest small. Only 130 of `src/`'s 363 lines are string literals a
user reads; the rest is comment prose.

Six areas with disjoint files, to land **after** #PR4Q, #WC3E and #JNYN — all three hold files this
rename touches (`board.py`, `board_tools.py`, `board_protocol.py`, `board_policy.md`, the briefs,
`test_history.py`, `BoardPane.cpp`, `docs/AGENT-SESSIONS-PROTOCOL.md`, `docs/SWITCHBOARD-FORMAT.md`,
`issues/POLICY.md` and four test files). A — the folder in the backend; B1 — the folder in the GUI;
B2 — the product word in the GUI; C — worker briefs, roles text, `app/` and `remote/`; D — docs and
site prose; E — the three doc renames, last, as a mechanical path rewrite.

Identifiers: `Glyph::Switchboard` (it names the icon, which is kept), `kReasonSwitchboard` (its
value is on disk in the project registry), the `switchboard` role and agent-context ids (wire names
and QSettings keys), the `relay:switchboard-policy` pointer markers (`_with_pointer` finds the old
block by those exact bytes) and the `switchboard` card label all stay. `RELAY_GLOBAL_SWITCHBOARD`
and `$XDG_CONFIG_HOME/relay/switchboard` are a later card of their own.

Four questions on the card for the owner: `board/` vs `.board/` and whether the "Hidden Switchboard
folder" option survives; "Board agent" in the helper header and whether "Switchboarding" stays;
whether the `switchboard` role id is renamed; whether the doc files are renamed. Recommendations on
each. `docs/SWITCHBOARD-AESTHETIC.md` is recommended to keep its name — it is the document about
the aesthetic the owner is keeping, and it is what the theme files cite.
<!-- relay:entry 20260921T235808Z-d2 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 23:58
Owner answered the four questions ("1 about about _board rather than board? (i dont feel strongly about this) 2 yes 3 what is the role id? 4 ok"); recorded in `## Decisions`. Implementation waits for #PR4Q, #WC3E and #JNYN to land, then one subagent per area in the plan's order.
<!-- relay:entry 20260922T003445Z-p3 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 00:34
#PR4Q, #WC3E and #JNYN have landed; claimed. Areas A, B (B1+B2), C and D start as four Opus subagents with disjoint files; E (the doc renames) follows when they have landed.
