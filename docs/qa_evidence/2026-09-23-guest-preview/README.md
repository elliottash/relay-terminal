# Guest preview content (#P9SN)

`PYTHONPATH=backend:tests python3 -m unittest test_guest_sessions test_conv_index` passed all 231 tests. New fixture cases cover a Relay plugin/AGENTS preamble, a handover context followed by a real user request, setup-only Claude text, and an unchanged transcript with an old parser cursor. Claude and Codex list rows use “Please fix the pane” as `first_prompt` and `snippet` in the mixed fixture. The setup-only row has neither field. The old cursor is reparsed once, so existing index rows refresh without modifying guest transcripts.
