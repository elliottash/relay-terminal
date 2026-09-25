# Relay Board verification backlog audit

Snapshot: 2026-09-24, live shared checkout. This is a read-only audit of card and policy files; other sessions were editing the Board during the count. Counts describe file contents at the command's execution, not an immutable revision. The Board index may be stale, so counts use card front matter. Sources below are repository paths, linked relative to this note.

## Lane size, age, and metadata

The final count found **706 work cards**: needs-verification 306; needs-qa-llm 241; needs-qa-human 0; planned 42; executing 14; inbox 10; discussing 23; planning 3; deferred 4; done 53; dropped 10. Thus the two populated verification and QA lanes contain **547 cards**. A first run moments earlier found 705 total and planning 2; the checkout changed concurrently. The status is a queue location, not proof that any card passed. Source: `.board/{features,changes,design,marketing,planning,memory,aliases}/**/*.md` front matter; counting command below.

| Field or structural signal | Needs verification (306) | Needs QA LLM (241) |
|---|---:|---:|
| `verify` block | 54 | 0 |
| `qa` block | 0 | 0 |
| `## Done means` with content | 195 | 3 |
| `## QA checklist` with content | 112 | 216 |
| `## Verdict` heading | 6 | 7 |
| `## Tests` with content | 243 | 4 |
| Passing latest `### Check` under Tests | 7 | 0 |
| `links.evidence` nonempty | 285 | 167 |
| `links.commits` nonempty | 244 | 97 |
| `implemented_by` nonempty | 230 | 226 |
| `verified_by` nonempty | 0 | 0 |
| `unverified_reasons()` empty | 13 | 7 |
| Missing primary evidence per `unverified_reasons()` | 292 | 234 |

Counts overlap. A Verdict heading is only a syntactic signal under the current implementation; it does not establish a favorable verdict or independent review. `unverified_reasons()` is a structural predicate, not a quality judgment. Created-date ages as of 2026-09-24: needs-verification median 2 days (110 aged 0–1 days, 195 aged 2–6, 1 aged 7+); needs-qa-llm median 6 days (0 aged 0–1, 164 aged 2–6, 77 aged 7+). In the 54 `verify` blocks in needs-verification, `human` is none 30, optional 21, required 3; `stakes` is nuisance 5, rework 23, money 1, and absent 25. `effort` is low 20, medium 31, high 3. `primary` is script 51, probe 2, person 1. `sign_off: none` appears in 52. Thus risk markers are absent from most backlog cards. Source: same card snapshot and command; gate definitions in [board.py](../../backend/relay_core/board.py) and [qa_policy.py](../../backend/relay_core/qa_policy.py).

### Reproduce

Run from repository root. This deliberately uses the implementation's predicates for the evidence gate; it only reads files. The card set may change while it runs.

```bash
PYTHONPATH=backend python3 - <<'PY'
from pathlib import Path
from collections import Counter, defaultdict
from datetime import date
from types import SimpleNamespace
import yaml
from relay_core import board as B
lanes=defaultdict(Counter); ages=defaultdict(list); risks=defaultdict(Counter)
for tab in 'features changes design marketing planning memory aliases'.split():
    for p in (Path('.board')/tab).rglob('*.md'):
        s=p.read_text()
        if not s.startswith('---\n'): continue
        try: f=yaml.safe_load(s.split('---',2)[1]); b=s.split('---',2)[2]
        except Exception: continue
        if not isinstance(f,dict) or not f.get('id'): continue
        lane=f.get('status'); c=lanes[lane]; c['cards']+=1
        if lane not in ('needs-verification','needs-qa-llm','needs-qa-human'): continue
        links=f.get('links'); links=links if isinstance(links,dict) else {}
        flags={'verify':f.get('verify') is not None,'qa':f.get('qa') is not None,
          'done_means':bool(B.section_text(b,'Done means').strip()),
          'qa_checklist':bool(B.section_text(b,'QA checklist').strip()),
          'verdict':B.has_verdict(b),'tests':bool(B.section_text(b,'Tests').strip()),
          'passing_check':B.check_passing(b),'evidence':bool(links.get('evidence')),
          'commits':bool(links.get('commits')),'implemented_by':bool(f.get('implemented_by')),
          'verified_by':bool(f.get('verified_by'))}
        c.update(k for k,v in flags.items() if v)
        reasons=B.unverified_reasons(SimpleNamespace(type='work',front=f,body=b))
        if not reasons: c['structural_verified']+=1
        for r in reasons:
            key=('invalid_verify' if 'invalid' in r else 'deferred' if 'verify.deferred' in r
              else 'missing_primary' if 'primary evidence' in r else 'human_answer'
              if "person's answer" in r else 'receipt' if 'Receipt:' in r else 'other')
            c[key]+=1
        v=f.get('verify')
        if isinstance(v,dict):
            risks[lane].update(f'{k}:{v[k]}' for k in ('human','stakes','effort','primary','sign_off') if k in v)
        try: ages[lane].append((date(2026,9,24)-date.fromisoformat(str(f['created']))).days)
        except Exception: c['bad_created']+=1
for lane,c in sorted(lanes.items()):
    a=sorted(ages[lane]); print(lane,dict(c))
    if a: print(' age median, 0-1, 2-6, 7+: ',a[len(a)//2],sum(x<=1 for x in a),sum(2<=x<=6 for x in a),sum(x>=7 for x in a))
    if risks[lane]: print(' risk:',dict(risks[lane]))
PY
```

## Current policy and exact closure gates

Project [board.yaml](../../.board/board.yaml) has no `qa:` override. [qa_policy.py](../../backend/relay_core/qa_policy.py) defaults to `verification: ask`, `ask_at_stakes: money`, `ai_may_gate_after: never`, and `sample_after: never`. A global Options override can change the effective policy, so the file alone cannot prove the live UI setting. The policy raises `human` to required at money or greater stakes; AI text or visual primary review becomes advisory unless permitted and independent; sampling is disabled by default. The relevant prior implementation card is [#C3Q2](../../.board/features/2026-09-23-automatic-or-not-a-qa-policy-floor-in-options-an.md), still needs-verification itself. It records the Options switch, project override, tests and commits, but has no `links.evidence`, no Verdict, and no passing Check on the card. Its status and listed test commands do not prove its current revision passed.

The structural predicate in [board.py](../../backend/relay_core/board.py) requires a readable `verify` block when present, no `verify.deferred`, primary evidence (`## Verdict` or a passing latest `### Check` under `## Tests`), all required human questions answered, and a `Receipt:` for required sign-off. No verify block means only the primary-evidence condition applies. The move path in [board_tools.py](../../backend/relay_core/board_tools.py) additionally checks an open signal attributed to the card before it leaves needs-verification, refuses agent closure over any unanswered numbered Human QA question, refuses deferred moves into QA or done, and enforces the ask-mode owner-close rule for an agent closing a card with a non-required-human plan from verification or QA lanes. Under automatic mode, only that latter ask gate is relaxed. [BOARD-FORMAT.md](../../docs/BOARD-FORMAT.md) also specifies a test-result gate on leaving needs-verification: listed checks failed or missing for the relevant revision block the move unless explicitly overridden; cards with no Tests are prompted once for checks. Signal and test-run state are not fully represented by card metadata, so the 20 structurally eligible cards are **not** a count of cards mechanically closable now.

Representative blockers: [#MH7P](../../.board/features/2026-09-22-helper-agent-on-the-models-pane.md) has commit and evidence links but no primary Verdict or passing Check. [#A0SF](../../.board/features/needs_qa_llm/2026-09-19-dragging-a-pane-onto-the-tab-pane-can-be-done-on.md) has commit and evidence links and is already in QA LLM, yet lacks primary evidence by the current predicate. [#M9T4](../../.board/features/needs_qa_llm/2026-09-17-subagents-ui.md) has no commit or evidence links and no primary evidence. [#Z82M](../../.board/features/2026-09-24-relay-free-serves-images-a-relay-image-role-on-t.md) declares money stakes and required human review; its person answer and primary evidence are missing. [#KQNP](../../.board/features/2026-09-23-show-and-use-banked-usage-resets-claude-count-us.md) has an invalid parsed `verify` block due to an unquoted apostrophe in a flow mapping, so the verification predicate rejects it. These are observations about recorded metadata, not judgments about the underlying implementations.

## Design alternatives and limits

The related [#BX7B design card](../../.board/design/2026-09-23-broaden-verification-into-a-qa-pane-with-card-sp.md) proposes a general QA ladder and a separate Review pane for judgments that actually need a person. Its alternatives include deterministic scripts, live probes, metrics, AI review, pairwise review, a person, and deferred world checks. It also notes the unresolved distinction between optional and required human review. The [server/case design record](../../.board/design/2026-09-23-cards-build-servers-servers-serve-cases-skills-a.md) proposes inherited server risk profiles, a case ledger, earned authority and sampling; those are design proposals, not evidence that this backlog qualifies for bulk closure. The newer [#P7CF card](../../.board/design/2026-09-24-risk-based-verification-and-backlog-closure.md) explicitly asks for risk-based backlog research and warns that status alone does not prove quality.

For backlog reduction, the mechanical sequence is: establish the effective global QA setting; repair malformed verify blocks; run or review the card's declared checks against the current revision; record the verifier's actual result as Verdict or passing Check and any required Human QA answer or Receipt; resolve attributable open signals; then use the authorized closure path. For legacy cards without `verify` or `qa`, missing risk markers prevent reliable automatic risk stratification. Any mass treatment of them would need an explicit policy decision and a separately recorded audit trail. This audit did not inspect every artifact or run the tests, did not read private signal state or the user's global Options setting, and did not treat a section heading as a substantive pass.
