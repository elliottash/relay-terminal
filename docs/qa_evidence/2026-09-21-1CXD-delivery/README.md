# #1CXD delivery

Resumed the owner's interrupted rename, preserving other sessions' changes.

- 82acbc04993a: GUI creates board/, discovers board/.switchboard/switchboard/issues in backend order,
  offers an explicit Move this board to board/ action for the two older spellings, removes the
  hidden-folder preference. Legacy issues/ is unchanged. Exact proposed tree passed land build gate.
- e4377755c8cb: three BOARD-* documents and redirect stubs at old names; current links updated.
- fe82d4344e98: audited remaining live backend tool descriptions, prompt blocks, refusals,
  generated headers, Try it text and CLI help. Visible names use Board; identifiers remain stable.
- Area A's trailing test assertion was already landed as 42ca4995; no recovery edit was needed.

Validation: projects, boardworkspace, boardsections, projectinit, board and boardpane initially
passed 6/6; two focused backend batches passed 848 and 834 tests. See tests.txt for commands.
New driver evidence at ../2026-09-21-74Y5-named-drive/ shows the Board in a real isolated GUI on a
newly staged board/ fixture; historical source comments, card threads/evidence and compatibility
identifiers still contain the old word. No icon or aesthetic assets were changed.

The repository-wide board format check still reports pre-existing errors in unrelated cards and
threads (13 errors, 745 warnings at first check); no unrelated record was repaired for this rename.
Independent verification must assess the card's exact criteria, including the residual historical
comment text, rather than interpret a raw grep count as product-visible wording.
