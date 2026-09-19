"""Card #N3WC repro: a sentence that names a file got "command not found" under its echo.

Run:  RELAY_KEYRING=off PYTHONPATH=backend python3 docs/qa_evidence/2026-09-18-command-not-found-sentence-naming-a-file/repro.py

`net` and `znew` are real programs on the owner's machine (one edit from "new"), so the
table below states them as installed instead of asking the machine.
"""
from relay_core.router import FixedCommands, classify

INSTALLED = FixedCommands("git python make ls net znew cd".split())

OWNER = ("new card: the introductory agent instructions file was still showing when i didnt "
         "have any. it shouldnt show any in that case. just init the default RELAY.MD\n\n")

CASES = [
    ("owner line (verbatim, session 4e62ce12 turn da613e73)", OWNER, False),
    ("colon mid-line, file named", "gti notes: the commit should explain why relay.md changed, and nothing else", False),
    ("full stop mid-line, file named", "gti log shows relay.md was committed. can you check", False),
    ("full stop mid-line, path named", "pyton note: scripts/train.py OOMs again. can you look", False),
    ("real slip with a file operand keeps the note", "gti stauts report.txt", True),
    ("a full stop on the LAST word is not a sentence break", "gti stauts.", True),
    ("plain slip keeps the note", "gti status", True),
    ("typo of cd with a dot-dot operand keeps the note", "cs ..", True),
]

wrong = 0
for label, text, want in CASES:
    d = classify(text, path=INSTALLED)
    ok = d.explain_invalid == want and d.route == "agent"
    wrong += not ok
    print(f"{'ok  ' if ok else 'FAIL'} explain={str(d.explain_invalid):5} (want {str(want):5}) "
          f"{label}\n     {d.invalid_reason or '-'} | {d.reason}")
print(f"\n{wrong} wrong of {len(CASES)}")
