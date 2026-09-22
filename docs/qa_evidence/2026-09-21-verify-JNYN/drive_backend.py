#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""#JNYN verifier's own drive of `relay_core.tryit_protocol` (not the implementer's tests).

Run against a clean export:

    X=<export>; PYTHONPATH=$X/backend:$X/tests RELAY_KEYRING=off \
        python3 docs/qa_evidence/2026-09-21-verify-JNYN/drive_backend.py

Each numbered block is one `## Done means` claim.  It prints what it saw, and asserts nothing it
did not print, so the transcript is the evidence.
"""
import datetime
import sys
import tempfile
from pathlib import Path

from relay_core import board as B
from relay_core import board_protocol as P
from relay_core import board_tools as T
from relay_core import tryit_protocol as TI
from relay_core.provider import ProviderConfig

CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""
TODAY = datetime.date.today().isoformat()
FAILS = []


def check(label, ok, detail=""):
    print(f"  [{'PASS' if ok else 'FAIL'}] {label}{(' — ' + detail) if detail else ''}")
    if not ok:
        FAILS.append(label)


class Turns:
    """The worker's TurnSupervisor, faked: it records what was submitted and never calls a model."""

    def __init__(self):
        self.submitted, self.resets, self.cancels, self.busy = [], 0, 0, False
        self.agent = None

    def reset(self):
        self.resets += 1

    def submit(self, prompt, when="now", request_id=None, context=None, attachments=None, **kw):
        self.submitted.append(prompt)
        return "q1"

    def cancel(self):
        self.cancels += 1

    def now_or_later(self, now, later):
        return later() if self.busy else now()


class Agent:
    def __init__(self, tools):
        self.board = tools
        self.config = ProviderConfig(api_key="", base_url="https://example.invalid",
                                     model="stub/one")
        self.session_id = "s-verify"


class Bench:
    def __init__(self, tmp):
        self.repo = Path(tmp).resolve()
        (self.repo / "issues").mkdir()
        (self.repo / "issues" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.board = B.Board(self.repo / "issues", self.repo)
        self.events = []
        self.turns = Turns()
        self.commands = P.BoardCommands(self.turns, self.events.append)
        self.commands.configure(str(self.repo), {})
        self.tools = self.commands.agent_tools(str(self.repo), {})
        self.turns.agent = Agent(self.tools)
        self.card = self.create()

    def create(self):
        events = self.send(type="board_create", id="r1", tab="features", status="inbox",
                           title="Order totals are wrong above 100 items",
                           text="totals go negative past a hundred lines")
        return [e for e in events if e["event"] == "board_written"][0]["card_id"]

    def send(self, **request):
        self.events.clear()
        self.commands.dispatch(request)
        return list(self.events)

    def tryit(self, events=None):
        return [e for e in (events if events is not None else self.events)
                if e.get("event") == "tryit"]

    def body(self):
        return self.board.card_by_id(self.card).body

    def put_section(self, heading, text):
        card = self.board.card_by_id(self.card)
        r = self.tools.run("board_update_card",
                           {"id": self.card, "base_hash": B.file_hash(card.path),
                            "replace_section": {"heading": heading, "text": text}})
        assert "error" not in r, r
    def finish(self, outcome="done", text="Staged it; one question on the card."):
        self.events.clear()
        if text:
            self.commands.observe({"event": "delta", "text": text})
        self.commands.observe({"event": outcome, "turn_id": "t-9"})
        return list(self.events)


def bench():
    tmp = tempfile.mkdtemp(prefix="jnyn-")
    return Bench(tmp)


# 1 --------------------------------------------------------------- stages itself, no Verify dir
print("\n1. try_run with NO Verify staging: it stages and plays the pass itself")
b = bench()
started = b.tryit(b.send(type="try_run", id="k1", card=b.card))[0]
print(f"     started: {dict((k, started[k]) for k in ('state','card_id','out','reusing','staged'))}")
check("started, reusing false, staged empty",
      started["state"] == "started" and started["reusing"] is False and started["staged"] == "")
check("evidence dir made before the brief is sent",
      (b.repo / started["out"]).is_dir(), started["out"])
check("out is docs/qa_evidence/<date>-tryit-<ID>",
      started["out"] == f"docs/qa_evidence/{TODAY}-tryit-{b.card}")
p = b.turns.submitted[-1]
check("brief says to stage it itself", "No staged environment from the verifying session" in p)
check("brief carries the card's Issue text", "totals go negative past a hundred lines" in p)
check("brief carries ## Done means instruction", "Done means" in p)
check("expected result sealed and NOT on the card",
      "expected.md" in p and "sealed" in p.lower() and "and nowhere on the card" in p)
check("its own conversation (turns.reset)", b.turns.resets == 1)
Path("/tmp/jnyn-brief.txt").write_text(p, encoding="utf-8")

# 2 --------------------------------------------------------------- already running
print("\n2. a second try_run while one is in flight")
again = b.tryit(b.send(type="try_run", id="k2", card=b.card))[0]
print(f"     {again['state']}: {again['message']}")
check("refused with 'already running'",
      again["state"] == "error" and "already running" in again["message"])
check("no second turn submitted", len(b.turns.submitted) == 1)

# 3 --------------------------------------------------------------- staging failure
print("\n3. a turn that could not stage: a note, and NO ## Try it")
b.tools.run("board_comment", {"id": b.card, "kind": "note",
                              "text": "Try it could not be staged: build/relay is not built here."})
done = b.tryit(b.finish(text="I could not stage it."))[-1]
print(f"     finished: section_written={done['section_written']} "
      f"staging_failed={done['staging_failed']}\n     message: {done['message']}")
check("staging_failed true, section_written false",
      done["staging_failed"] is True and done["section_written"] is False)
check("no ## Try it section on the card", "## Try it" not in b.body())
notes = [e for e in b.board.thread(b.card) if e.kind == "note"]
check("the note is on the thread, beginning with the fixed sentence",
      bool(notes) and notes[-1].text.startswith(TI.FAILED_NOTE))

# 3b ------------------------------------------------- the worker's own note (39bab9d9)
print("\n3b. a turn whose provider stalled: the WORKER leaves the note (39bab9d9)")
bs = bench()
bs.send(type="try_run", id="k1", card=bs.card)
ev = bs.tryit(bs.finish(outcome="error", text=""))[-1]
print(f"     {ev['state']} outcome={ev.get('outcome')} section_written={ev['section_written']} "
      f"staging_failed={ev['staging_failed']}\n     message: {ev['message']}")
notes = [e for e in bs.board.thread(bs.card) if e.kind == "note"]
check("the worker wrote the note although the agent never did",
      bool(notes) and notes[-1].text.startswith(TI.FAILED_NOTE))
check("the note says the card is not asking to be reviewed",
      bool(notes) and "not asking to be reviewed" in notes[-1].text)
check("still no ## Try it section", "## Try it" not in bs.body())
print("\n3c. the agent's own note is never written over")
bs2 = bench()
bs2.send(type="try_run", id="k1", card=bs2.card)
bs2.tools.run("board_comment", {"id": bs2.card, "kind": "note",
                                "text": "Try it could not be staged: the agent said so itself."})
bs2.finish(outcome="error", text="")
notes2 = [e for e in bs2.board.thread(bs2.card) if e.kind == "note"]
check("one note only, the agent's", len(notes2) == 1 and "itself" in notes2[-1].text,
      f"{len(notes2)} note(s)")

# 4 --------------------------------------------------------------- reuse of the Verify staging
print("\n4. try_run REUSING docs/qa_evidence/<date>-verify-<ID>/ (owner's step 3)")
b2 = bench()
vdir = b2.repo / "docs" / "qa_evidence" / f"{TODAY}-verify-{b2.card}"
vdir.mkdir(parents=True)
(vdir / TI.STAGE_SCRIPT).write_text("#!/bin/bash\necho staged\n", encoding="utf-8")
(vdir / TI.STAGING_NOTES).write_text("The seeded orders are invented.\n", encoding="utf-8")
started = b2.tryit(b2.send(type="try_run", id="k1", card=b2.card))[0]
print(f"     started: reusing={started['reusing']} staged={started['staged']}")
p2 = b2.turns.submitted[-1]
check("reusing true and staged names the script",
      started["reusing"] is True and started["staged"].endswith(TI.STAGE_SCRIPT))
check("brief says ALREADY STAGED and not to replay",
      "ALREADY STAGED by the verifying session" in p2
      and "do not replay the mechanical steps" in p2)
check("brief names the staging notes", TI.STAGING_NOTES in p2)

print("\n4b. a `staged:` line in the card body wins over the glob")
named = b2.repo / "docs" / "qa_evidence" / "2026-09-01-verify-elsewhere"
named.mkdir(parents=True)
(named / TI.STAGE_SCRIPT).write_text("#!/bin/bash\n", encoding="utf-8")
b2.put_section("QA checklist",
               f"staged: {named.relative_to(b2.repo)}/\nsimulation: drove the gate with xdotool\n")
found = TI.verify_staging(b2.repo, b2.card, b2.body())
print(f"     verify_staging -> dir={found['dir']} simulation={found['simulation']!r}")
check("the named dir wins", found["dir"] == str(named.relative_to(b2.repo)))
check("the simulation line is carried through", "drove the gate" in found["simulation"])

print("\n4c. a verify dir with no stage script falls through to staging itself")
b3 = bench()
(b3.repo / "docs" / "qa_evidence" / f"{TODAY}-verify-{b3.card}").mkdir(parents=True)
st3 = b3.tryit(b3.send(type="try_run", id="k1", card=b3.card))[0]
check("reusing false", st3["reusing"] is False,
      f"brief: ...{'no ' + TI.STAGE_SCRIPT + ' in it' in b3.turns.submitted[-1]}")

# 5 --------------------------------------------------------------- try_answer end to end
print("\n5. try_answer: verdict on the thread, the seal broken, ## Human QA generated")
b4 = bench()
out = TI.evidence_dir_for(b4.repo, b4.card)
out.mkdir(parents=True, exist_ok=True)
(out / TI.EXPECTED_FILE).write_text(
    "The move is refused and the notice names the two checks.\nA second line, to see the indent.\n",
    encoding="utf-8")
rel = str((out / TI.EXPECTED_FILE).relative_to(b4.repo))
b4.put_section("Try it",
               f"1. Open it: `bash docs/qa_evidence/{TODAY}-verify-{b4.card}/stage.sh`\n\n"
               "2. The task: open the card the fixture put in Needs verification and move it to Done.\n\n"
               "3. The question: did it stop you, and did you know why?\n\n"
               f"Expected: {rel} (sealed until you answer)\n")
print("     --- ## Try it before the answer ---")
print("\n".join("       " + l for l in T.section_text(b4.body(), "Try it").strip().splitlines()))
check("the expected text is NOT on the card before the answer",
      "The move is refused" not in b4.body())
answered = b4.tryit(b4.send(type="try_answer", id="a1", card=b4.card,
                            answer="It stopped me, and the notice said which check."))[-1]
print(f"     answered: expected_revealed={answered['expected_revealed']} "
      f"human_qa={answered['human_qa']}")
decisions = [e for e in b4.board.thread(b4.card) if e.kind == "decision"]
print("     --- the thread's decision entry ---")
print("\n".join("       " + l for l in decisions[-1].text.strip().splitlines()))
check("a `decision` entry quoting the person, named as the Try it verdict",
      bool(decisions) and "Try it · verdict" in decisions[-1].text
      and '"It stopped me, and the notice said which check."' in decisions[-1].text)
section = T.section_text(b4.body(), "Try it")
print("     --- ## Try it after the answer ---")
print("\n".join("       " + l for l in section.strip().splitlines()))
check("the seal is broken under ## Try it",
      "The move is refused" in section and rel in section)
human = T.section_text(b4.body(), "Human QA")
print("     --- ## Human QA (generated) ---")
print("\n".join("       " + l for l in human.strip().splitlines()))
check("Human QA holds the question, the answer and the expected result",
      "did it stop you" in human.lower() and "It stopped me" in human
      and "The move is refused" in human and "docs/qa_evidence" in human)
check("the revealed expected result is indented as ONE block (a739dd45)",
      all(l.startswith(("   ", "\t")) or not l.strip()
          for l in human.splitlines()
          if "A second line, to see the indent." in l or "The move is refused" in l))
open_q = T.unanswered_human_qa(b4.body())
print(f"     board_tools.unanswered_human_qa(body) -> {open_q}")
check("the close gate reads it as ANSWERED (interlock)", open_q == [])
check("the question really is in the numbered/indented shape the gate needs",
      any(l.strip()[:2].rstrip(".)").isdigit() for l in human.splitlines()))

print("\n5b. the gate before the answer says the question is open")
b5 = bench()
b5.put_section("Human QA", "1. Did it stop you, and did you know why?\n")
print(f"     unanswered_human_qa -> {T.unanswered_human_qa(b5.body())}")
check("an unanswered question holds the card", len(T.unanswered_human_qa(b5.body())) == 1)

print("\n5c. a second answer refreshes the summary and does not reveal twice")
b4.send(type="try_answer", id="a2", card=b4.card, answer="Second time: it was obvious.")
section2 = T.section_text(b4.body(), "Try it")
human2 = T.section_text(b4.body(), "Human QA")
check("Expected pasted once only", section2.count("The move is refused") == 1,
      f"count={section2.count('The move is refused')}")
check("Human QA carries the newest answer, one Answer: line",
      "Second time: it was obvious." in human2 and human2.count("Answer:") == 1)

print("\n5d. an answer with no ## Try it section, and an empty answer, are refused")
b6 = bench()
r1 = b6.tryit(b6.send(type="try_answer", id="a1", card=b6.card, answer="x"))[0]
print(f"     no section -> {r1['state']}: {r1['message']}")
check("refused, told to press Try it first",
      r1["state"] == "error" and "press Try it first" in r1["message"])
b6.put_section("Try it", "Open it: `bash x.sh`\n\nTry the thing.\n\nWas it clear?\n")
r2 = b6.tryit(b6.send(type="try_answer", id="a2", card=b6.card, answer="   "))[0]
print(f"     empty answer -> {r2['state']}: {r2['message']}")
check("an empty answer is refused", r2["state"] == "error")
r3 = b6.tryit(b6.send(type="try_answer", id="a3", card=b6.card, answer="No."))[-1]
check("a section with no sealed file still records the verdict",
      r3["state"] == "answered" and r3["expected_revealed"] is False)
check("…and Human QA has the indented Answer with no Expected",
      "Answer: No." in T.section_text(b6.body(), "Human QA")
      and "Expected:" not in T.section_text(b6.body(), "Human QA"))

# 6 --------------------------------------------------------------- try_stop
print("\n6. try_stop ends the run cleanly, and there is then nothing to stop")
b7 = bench()
b7.send(type="try_run", id="k1", card=b7.card)
b7.send(type="try_stop", id="k2")
ended = b7.tryit(b7.finish(outcome="cancelled", text=""))[-1]
print(f"     {ended['state']}; cancels={b7.turns.cancels}; running={b7.commands._tryit().running()}")
check("stopped, the turn cancelled, nothing running",
      ended["state"] == "stopped" and b7.turns.cancels == 1
      and not b7.commands._tryit().running())
nothing = b7.tryit(b7.send(type="try_stop", id="k3"))[0]
check("a second stop is an error", nothing["state"] == "error", nothing["message"])

# 7 --------------------------------------------------------------- board_try, and its gate
print("\n7. board_try: the same brief for a terminal pane, gated like board_claim")
b8 = bench()
res = b8.tools.run("board_try", {"card": b8.card})
print(f"     board_try -> card={res['card']} evidence_dir={res['evidence_dir']} "
      f"reusing={res['reusing']}")
check("hands back the brief, not a turn",
      "[Switchboard Try it]" in res["text"] and "expected.md" in res["text"])
check("evidence dir is the same convention",
      res["evidence_dir"] == f"docs/qa_evidence/{TODAY}-tryit-{b8.card}")
prog = [e for e in b8.board.thread(b8.card) if e.kind == "progress"]
check("a progress entry is appended", bool(prog) and "preparing Try it" in prog[-1].text)
b8.tools.begin_cleanup("c-1")
ref = b8.tools.run("board_try", {"card": b8.card})
clm = b8.tools.run("board_claim", {"id": b8.card})
print(f"     during a cleanup: board_try -> {ref['code']}; board_claim -> {clm['code']}")
check("refused during a cleanup exactly as board_claim is",
      ref["code"] == "board_refused" and clm["code"] == "board_refused"
      and "not available during a Switchboard cleanup" in ref["error"])
b9 = bench()
b9.send(type="board_cleanup", id="c1", dry_run=True)
busy = b9.send(type="try_run", id="k1", card=b9.card)[0]
check("try_run is refused while a cleanup runs",
      busy.get("code") == "board_busy", str(busy.get("code")))
b10 = bench()
bad = b10.tryit(b10.send(type="try_run", id="k1", card="ZZZZ"))[0]
check("an unknown card is refused with a sentence", "no card #ZZZZ" in bad["message"])
b10.turns.agent = None
none = b10.tryit(b10.send(type="try_run", id="k2", card=b10.card))[0]
check("no provider configured is refused with a sentence",
      "Configure a provider" in none["message"], none["message"])

print("\n" + "=" * 70)
print(f"{'ALL CHECKS PASSED' if not FAILS else 'FAILURES: ' + ', '.join(FAILS)}")
sys.exit(1 if FAILS else 0)
