"""The QA policy floor (card #C3Q2): "automatic or not", as three silent rules.

Design card #BX7B's ladder ends with a question the user never fills in: which stakes always
need a person, which rungs may gate on their own, whether a check may be sampled.  The owner's
steer (2026-09-23) was that "most of this is just in the agent's work and the user doesn't see it
directly", so the answer is a **floor** the worker applies to every proposed `verify` block, and
the user meets exactly one switch — Options › Agent › QA › *Verification*: *ask* (default) or
*automatic* — plus, for a project that wants to tune the floor by hand, a `qa:` block in
`board.yaml` whose values override the global ones.

Three layers, project over global over the defaults in code::

    qa:                          # board.yaml, all keys optional
      verification: ask          # ask | automatic
      ask_at_stakes: money       # nuisance | rework | money | reputation | harm | never | always
      ai_may_gate_after: never   # never | always | <N> (verified cases the reviewer has on record)
      sample_after: never        # never | always | <N>

`parse` builds the policy, `apply` runs the three rules over one block and says what it did
(agent-facing notes for the tool result, never text a user reads), and `effective_line` is the
one-line statement `board_read` carries so an agent can see the floor it works under.  Pure
functions: no file, no board, no clock.  The counters `<N>` compare against are the case ledger
of #95VZ (`relay_core.cases`): `BoardTools._qa_floor` passes ``cases=`` the number of passing
rows for the server that serves the card (`cases.verified_count`), so an integer is a real
threshold, and a caller with no ledger passes 0, which reads as "not yet".
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Mapping

from .board import VERIFY_STAKES

VERIFICATION = ("ask", "automatic")
#: The rungs an AI reviewer supplies.  Advisory (`also`) unless the policy lets them gate.
AI_MODES = ("ai-text", "ai-visual")
POLICY_KEYS = ("verification", "ask_at_stakes", "ai_may_gate_after", "sample_after")
DEFAULTS = {"verification": "ask", "ask_at_stakes": "money",
            "ai_may_gate_after": "never", "sample_after": "never"}
_STAKES_RANK = {word: i for i, word in enumerate(VERIFY_STAKES)}


@dataclass(frozen=True)
class QaPolicy:
    verification: str = "ask"
    #: The stakes word from which `human` is raised to `required`; `never` turns the floor
    #: off and `always` is `nuisance` spelled out.
    ask_at_stakes: str = "money"
    #: `never`, `always`, or the number of verified cases an AI reviewer needs on record before
    #: `ai-text` / `ai-visual` may be the `primary` (gating) rung.
    ai_may_gate_after: str | int = "never"
    #: Likewise for a `sample` line on the block.
    sample_after: str | int = "never"
    #: Where each key came from: "default", "global" (Options, via `configure.qa`) or
    #: "project" (`board.yaml qa:`).
    source: dict = field(default_factory=lambda: {k: "default" for k in POLICY_KEYS})
    #: Values that were not understood and fell back, each as one sentence.
    problems: tuple = ()


def _word(value) -> str:
    return str(value).strip().lower() if isinstance(value, (str, int, float)) \
        and not isinstance(value, bool) else ""


def _parse_value(key: str, value):
    """The normalized value for `key`, or None when `value` is not one it takes."""
    word = _word(value)
    if key == "verification":
        return word if word in VERIFICATION else None
    if key == "ask_at_stakes":
        if word == "always":
            return VERIFY_STAKES[0]
        return word if word in VERIFY_STAKES or word == "never" else None
    # ai_may_gate_after / sample_after: never | always | a positive whole number of cases
    if word in ("never", "always"):
        return word
    if isinstance(value, bool):
        return None
    if isinstance(value, int) and value > 0:
        return value
    if word.isdigit() and int(word) > 0:
        return int(word)
    return None


def parse(board_yaml_qa: Mapping | None = None, configure_qa: Mapping | None = None) -> QaPolicy:
    """The effective policy: `board.yaml`'s `qa:` block over `configure.qa` over `DEFAULTS`.

    A key that is missing at one layer falls through to the next; a value that is not one the
    key takes is skipped with a problem sentence and the next layer's value stands.  Neither
    argument has to be a mapping: anything else counts as an empty layer.
    """
    values = dict(DEFAULTS)
    source = {k: "default" for k in POLICY_KEYS}
    problems: list[str] = []
    for label, layer in (("global", configure_qa), ("project", board_yaml_qa)):
        if not isinstance(layer, Mapping):
            continue
        where = "board.yaml qa" if label == "project" else "Options"
        for key in POLICY_KEYS:
            if key not in layer or layer[key] is None:
                continue
            parsed = _parse_value(key, layer[key])
            if parsed is None:
                problems.append(f"{where}.{key} {layer[key]!r} is not a value it takes; "
                                f"using {values[key]!r}")
                continue
            values[key] = parsed
            source[key] = label
    return QaPolicy(verification=values["verification"], ask_at_stakes=values["ask_at_stakes"],
                    ai_may_gate_after=values["ai_may_gate_after"],
                    sample_after=values["sample_after"], source=source, problems=tuple(problems))


def _allowed(setting: str | int, cases: int) -> bool:
    if setting == "always":
        return True
    if setting == "never":
        return False
    return int(cases or 0) >= int(setting)


def _independent_verifier(qa_block: Mapping | None) -> bool:
    """Whether the card's `qa` block (protocol 19.15) names a verifier outside the author's
    lineage: a `recommended` entry that is not `same_lineage`."""
    if not isinstance(qa_block, Mapping):
        return False
    recommended = qa_block.get("recommended")
    return isinstance(recommended, Mapping) and not recommended.get("same_lineage", True)


def apply(policy: QaPolicy, verify: Mapping | None, qa_block: Mapping | None = None,
          *, cases: int = 0) -> tuple[dict | None, list[str]]:
    """The three rules over one normalized `verify` block (as `validate_verify` returns it).

    Returns the block to store and the notes, one sentence each, saying what changed and why;
    an empty list means the block stood.  None in, None out.

    1. **Stakes floor.**  `stakes` at or above `ask_at_stakes` raises `human` to `required`;
       a block without `criteria` gets one line so it still validates.
    2. **AI gating.**  `ai-text` / `ai-visual` as `primary` is moved to `also` unless the policy
       allows AI gating (`ai_may_gate_after` reached) *and* `qa_block` names a verifier outside
       the author's lineage.  The next non-AI rung in `also` becomes `primary`, or `person` when
       there is none — and then a person must look, so `human` is raised too.
    3. **Sampling.**  A `sample` line is dropped unless `sample_after` allows it.
    """
    if verify is None:
        return None, []
    out = dict(verify)
    out["also"] = list(out.get("also") or [])
    notes: list[str] = []

    def need_person(reason: str) -> None:
        if out.get("human") != "required":
            notes.append(f"qa policy: human {out.get('human', 'none')} → required ({reason})")
            out["human"] = "required"
        if not out.get("criteria"):
            out["criteria"] = "a person confirms the outcome before the card closes"
            notes.append("qa policy: criteria filled in; replace it with what the person checks")

    stakes = str(out.get("stakes") or "")
    floor = policy.ask_at_stakes
    if floor != "never" and stakes in _STAKES_RANK and _STAKES_RANK[stakes] >= _STAKES_RANK[floor]:
        need_person(f"stakes {stakes} is at or above the floor {floor}")

    primary = str(out.get("primary") or "")
    if primary in AI_MODES:
        allowed = _allowed(policy.ai_may_gate_after, cases)
        independent = _independent_verifier(qa_block)
        if not (allowed and independent):
            why = ("AI gating is off" if not allowed
                   else "the card's qa block names no verifier outside the author's lineage")
            rest = [m for m in out["also"] if m != primary]
            replacement = next((m for m in rest if m not in AI_MODES), "person")
            out["also"] = [primary] + [m for m in rest if m != replacement]
            out["primary"] = replacement
            notes.append(f"qa policy: primary {primary} → {replacement}, {primary} kept in also ({why})")
            if replacement == "person":
                need_person("the primary rung is person")

    if out.get("sample") and not _allowed(policy.sample_after, cases):
        notes.append(f"qa policy: sample {out['sample']!r} dropped (sampling is off)")
        out.pop("sample", None)

    return out, notes


def closes_automatically(policy: QaPolicy, verify: Mapping | None) -> bool:
    """Whether a verifier's pass may close this card without the user (the *automatic* switch):
    `verification: automatic` and a plan that needs no person (`human` is not `required`).
    A card with no plan is not judged here; in `ask` mode nothing closes automatically."""
    if policy.verification != "automatic" or verify is None:
        return False
    return verify.get("human") != "required"


def _after(setting: str | int) -> str:
    return setting if isinstance(setting, str) else f"after {setting} cases"


def effective_line(policy: QaPolicy) -> str:
    """The floor in one line, for `board_read` and the agent's own reading."""
    where = {"default": "", "global": " (Options)", "project": " (board.yaml)"}
    if policy.verification == "automatic":
        head = ("verification automatic" + where[policy.source["verification"]]
                + ": a verifier's pass closes a card whose plan needs no person")
    else:
        head = ("verification ask" + where[policy.source["verification"]]
                + ": every card waits in needs-verification for the user to close")
    floor = ("no stakes floor" if policy.ask_at_stakes == "never"
             else f"human required from stakes {policy.ask_at_stakes}")
    line = (f"{head}; {floor}; AI may gate {_after(policy.ai_may_gate_after)}; "
            f"sampling {_after(policy.sample_after)}")
    if policy.problems:
        line += "; ignored: " + " ".join(policy.problems)
    return line
