---
name: referee-report
description: Draft the support document for a referee report on a manuscript PDF, then leave the report and its recommendation level to the person. "referee this paper", "referee report support".
short: 'Referee-report support document from a manuscript PDF; the person writes the report.'
profile: |
  artifact: text
  primary: level
  also: ai-text
  human: required
  criteria: the recommendation level and every numbered point are the referee's own, checked against the manuscript before it is sent
  sign_off: send
  effort: high
  stakes: reputation
  blast: case
  regularity: routine
  executable: no
  rot: high
  rot_reason: drives the Editorial Manager web UI, which changes without notice
  confidential: yes
  money: no
---

# Referee report support

The manuscript is confidential: read it from its own folder, quote nothing from it on the
Board, and keep every note in the same folder.

1. Read the whole PDF once, then the introduction and the main results a second time.
2. Write the one-paragraph summary of the claim, the identification strategy and the data, in
   the authors' own terms.
3. List the contributions the paper claims, and for each one the closest prior work you know.
4. Check every citation you can reach: does the cited paper say what this paper says it says?
5. Write the numbered concerns, most serious first: identification, data, robustness, framing.
   Each one names the page and the table or figure it is about.
6. Propose a recommendation level (reject, major revision, minor revision, accept) with one
   sentence of reasoning, marked as a proposal.
7. Hand the document to the person. They write the report, set the level and send it through
   Editorial Manager themselves; nothing here sends anything.
