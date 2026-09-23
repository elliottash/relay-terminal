"""Refine's write rules (#6W9X) against a throwaway copy of this repository's real board.

Run from the repo root: python3 docs/qa_evidence/2026-09-23-refine-6W9X/refine_transcript.py
It copies issues/ to a temp dir, opens a Refine turn on #3BPH (an open bug card), and records
what each write attempt returns. Nothing in the real issues/ is touched.
"""
import json, shutil, sys, tempfile
from pathlib import Path
sys.path.insert(0, "backend")
from relay_core import board as B, board_tools as T

tmp = Path(tempfile.mkdtemp()).resolve()
shutil.copytree("issues", tmp / "issues")
board = B.Board(tmp / "issues", tmp)
tools = T.BoardTools(board, emit=lambda e: None,
                     context=T.ToolContext(actor="agent", model="transcript", pane="0"),
                     state_path=tmp / ".relay" / "rate.json")
tools.begin_turn("t-1")
card = "3BPH"
before = board.card_by_id(card)
print(f"card #{card}: status={before.status} labels={before.front.get('labels')}")
tools.begin_card_turn("refine", card)

def h():
    return tools.run("board_read", {"id": card})["hash"]

def show(what, result):
    brief = {k: result[k] for k in ("code", "error") if k in result} or {"ok": True, "changes": result.get("changes")}
    print(f"- {what}: {json.dumps(brief, ensure_ascii=False)}")

links = dict(before.front.get("links") or {})
links["related"] = sorted(set(links.get("related") or []) | {"BGRN", "48S3"})
show("links.related += BGRN, 48S3", tools.run("board_update_card", {"id": card, "base_hash": h(), "fields": {"links": links}}))
show("links with commits dropped", tools.run("board_update_card", {"id": card, "base_hash": h(), "fields": {"links": {"related": ["BGRN"]}}}))
show("labels += tests (a word the board uses)", tools.run("board_update_card", {"id": card, "base_hash": h(), "fields": {"labels": list(before.front.get("labels") or []) + ["tests"]}}))
show("labels += zzinvented", tools.run("board_update_card", {"id": card, "base_hash": h(), "fields": {"labels": ["bug", "zzinvented"]}}))
has_dm = bool(T._section_span(board.card_by_id(card).body, T.DONE_MEANS_HEADING))
show(f"## Done means (card has one already: {has_dm})", tools.run("board_update_card", {"id": card, "base_hash": h(), "replace_section": {"heading": "Done means", "text": "The boardexecute test passes at HEAD."}}))
show("## Issue rewrite", tools.run("board_update_card", {"id": card, "base_hash": h(), "replace_section": {"heading": "Issue", "text": "new words"}}))
show("## Plan", tools.run("board_update_card", {"id": card, "base_hash": h(), "replace_section": {"heading": "Plan", "text": "1. fix"}}))
show("retitle", tools.run("board_update_card", {"id": card, "base_hash": h(), "title": "Other"}))
show("move to ready", tools.run("board_move_card", {"id": card, "status": "ready", "reason": "r"}))
show("comment on #BGRN", tools.run("board_comment", {"id": "BGRN", "kind": "note", "text": "hi"}))
show("note on own card", tools.run("board_comment", {"id": card, "kind": "note", "text": "Same as / fixed before: none found.\nThe ask, sharper: ...\nQuestions: none."}))
after = board.card_by_id(card)
print(f"card #{card} after: status={after.status} labels={after.front.get('labels')} related={after.front['links'].get('related')}")
shutil.rmtree(tmp)
