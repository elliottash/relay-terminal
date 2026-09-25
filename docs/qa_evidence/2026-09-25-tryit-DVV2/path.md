# The path — #DVV2 Try-it (about 4 minutes, one question at the end)

1. **check** — Run the staged lifecycle once and read it:
   `docs/qa_evidence/2026-09-25-tryit-DVV2/stage.sh`
   (then `cat docs/qa_evidence/2026-09-25-tryit-DVV2/transcript.txt` if you want the captured
   run side by side). Compare what you see against your own judgement of the nine steps —
   does every transition (reclaimed / promoted / refused / swept / floor-refused) happen, and is
   the ledger output enough to tell who made what and why?
2. **person** — The rogue dir `/tmp/dvv2-tryit-rogue` is now sitting on the real `/tmp`, holding
   `only-copy.txt`. That is the exact situation this card is about: work that matters, in `/tmp`,
   outside every ledger. Decide as the owner would: delete it, or ledger it
   (`relay-scratch adopt --apply`, then find it in `relay-scratch ledger` as an orphan).
3. **check** — `relay-scratch check` — does the one-line verdict and the report's biggest-first
   list with the ledger line read like something you would act on during a real cleanup?
4. **person** — The question at the end.
