## Try it
1. Open it: `bash /tmp/claude-1000/jn-try/evidence/stage.sh`
2. The card in Needs verification says the fix is done and every test passes. Try to move it to
   Done with the status picker, and see whether the board lets you.
3. Did it stop you, and did it tell you why quickly enough that you would use it?

Expected: /tmp/claude-1000/jn-try/evidence/expected.md (sealed until you answer)

Expected: The move to Done is refused. The notice names the checks that do not prove the card —
`totals_large_order` failed the last time it ran, and `tests/test_invoice.py` has never run
here — and offers Override…, which asks for one line before it lets the card through.

## Human QA
1. Did it stop you, and did it tell you why quickly enough that you would use it?
   Answer: Yes - the picker snapped back and the notice named the two checks in about a second.

   Expected: The move to Done is refused. The notice names the checks that do not prove the card — `totals_large_order` failed the last time it ran, and `tests/test_invoice.py` has never run here — and offers Override…, which asks for one line before it lets the card through.

Generated from `## Try it` and the answer on the thread (opened with `bash /tmp/claude-1000/jn-try/evidence/stage.sh`, evidence in `/tmp/claude-1000/jn-try/evidence`). Press Try it again, or answer again, and it is rewritten.
