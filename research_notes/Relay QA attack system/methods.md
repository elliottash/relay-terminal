# Executable QA attack methods for Relay — card #SJTR

Research only, 2026-09-21. No implementation, installation, attack execution, card edits or commits. Local references describe the shared working tree observed during research (HEAD observed as `5d36e9cc`, with unrelated concurrent modifications); line numbers can drift. Relay board tools were absent from tool discovery, so card and thread were read from files. No agents launched. Recommendations below are proposals, not measured Relay outcomes.

## 1. Which methods find application failures, and what smallest harness is useful?

### Takeaway

Start with narrow executable targets and explicit failure rules. Coverage-guided fuzzing searches inputs; properties and metamorphic relations supply semantic checks; stateful generators search action sequences. These methods can be combined in one harness.

### Cited Findings

- **Verified — libFuzzer:** LLVM documents an in-process coverage-guided engine, a byte-array entry point (`LLVMFuzzerTestOneInput`), sanitizer combinations, corpus replay, and time/run limits. Targets should tolerate malformed inputs, reset state, and be deterministic. LLVM says important fixes remain supported but original authors stopped active feature development. [LLVM libFuzzer](https://llvm.org/docs/LibFuzzer.html)
- **Verified — AFL++:** Its maintainers recommend bypassing GUI interaction through a small input-processing executable, instrumenting relevant code and using persistent mode for throughput. They warn that real network channels are awkward for coverage-guided fuzzing; process a captured message stream locally where possible. [AFL++ best practices](https://aflplus.plus/docs/best_practices/)
- **Verified — stateful testing:** Hypothesis generates sequences of primitive actions and arguments, compares an implementation with a simplified model, supports invariants and produces short failing programs. Its `RuleBasedStateMachine.TestCase` integrates with `unittest`; pytest is not required. It is nevertheless an additional dependency. [Hypothesis stateful testing](https://hypothesis.readthedocs.io/en/latest/stateful.html)
- **Verified — metamorphic testing:** Chen, Cheung and Yiu propose deriving follow-up tests from successful cases using relations between executions, with the intent of revealing faults even where individual expected outputs are unavailable. This primary paper supports the method, not any claim that an arbitrary relation proposed by an agent is valid. [Original metamorphic-testing paper](https://arxiv.org/abs/2002.12543)
- **Verified — Qt integration:** Qt Test supports non-GUI tests, data-driven cases, mouse/keyboard simulation and selection of individual test functions/data tags. It can host fixed regression cases resulting from searches. [Qt Test overview](https://doc.qt.io/qt-6/qtest-overview.html)
- **Verified local seams:** The scanner consumes byte chunks and reports ordered sequence hits ([`engine/core/SequenceScanner.h:21`](/home/elliott/repos/relay-terminal/engine/core/SequenceScanner.h:21), `next` at line 36). The terminal interface exposes `feed`, `resize`, dimensions and ground state ([`engine/core/VtCore.h:58`](/home/elliott/repos/relay-terminal/engine/core/VtCore.h:58)), screen text and cursor state (lines 74–102). These are usable boundaries without terminal-window automation.
- **Verified local runners:** Engine tests link Qt Test and run with the offscreen platform ([`engine/CMakeLists.txt:124`](/home/elliott/repos/relay-terminal/engine/CMakeLists.txt:124)). Python discovery uses `python -m unittest` with isolated XDG paths ([`CMakeLists.txt:1165`](/home/elliott/repos/relay-terminal/CMakeLists.txt:1165)). Board tests already construct a temporary directory and board per test ([`tests/test_board.py:53`](/home/elliott/repos/relay-terminal/tests/test_board.py:53)).

### Inferences

**Recommended minimum pilots and qualitative costs — not benchmarks:**

| Method | Smallest useful Relay harness | Failure rule / evidence | Cost and realistic drawback |
|---|---|---|---|
| libFuzzer | One byte-buffer adapter around `SequenceScanner::next`; repeatedly consume hits and start a fresh scanner per input. Seed ESC, CSI, OSC, truncated sequences and split terminators. Compile target and exercised code with coverage and ASan/UBSan. | Sanitizer failure, crash, stalled progress; preserve exact bytes and build configuration. | Small target is cheap to execute after a separate instrumented build; malformed inputs may never reach deeper behavior. Sanitizer silence does not establish correct parsing. |
| AFL++ | Same logical scanner target with a file/stdin adapter; persistent loop only once per-input reset is demonstrated. | Same failure classes; retained corpus and reduced reproducer. | Another compiler/runner integration; persistent state leakage can create non-reproducible failures. A second engine should follow a working first harness rather than precede it. |
| Properties | A bounded generator over valid/invalid card fields, or terminal dimensions and feeds; assert a documented invariant after each operation. | Exact generated input plus violated invariant. | Low tool overhead with a stdlib generator or C++ loop; specification effort often dominates. An implementation copied into the oracle repeats its mistakes. |
| Stateful/model testing | Temporary board; a short vocabulary such as create, move, reload, inspect, plus an independently written map of expected identities/statuses. Check persisted and reloaded state after each action. | Concrete action list, model/implementation difference and reduced sequence. | Search space grows with sequence length. Overrestricting preconditions excludes real invalid transitions; overly permissive rules create false alarms. Hypothesis is an optional test dependency; a stdlib runner must implement reduction/replay itself. |
| Metamorphic testing | Feed identical terminal bytes in one chunk versus several chunks, recreating the same core and initial state for each execution. Compare final screen/cursor and normalized ordered callbacks. For scanner hits, convert chunk-relative offsets to absolute offsets. | Paired inputs, transformation and exact differing observable. | Cheap paired runs; relation validity needs review. Do not compare callback batching or intermediate paint timing unless the contract requires equality. |

Additional proposed properties worth discussing: reading terminal history must preserve viewport and dirty state; this is an explicit interface contract ([`engine/core/VtCore.h:92`](/home/elliott/repos/relay-terminal/engine/core/VtCore.h:92)). Canonicalization can be tested for idempotence only after identifying a function whose contract actually promises canonical form. Do not assume `decode(encode(x)) == x` for lossy formats or unrestricted Unicode normalization.

For C++ fuzzing, use a dedicated harness executable rather than adding libFuzzer's `main` to an existing Qt Test executable. Start at the scanner; a fuller core harness can install in-memory event callbacks and cap dimensions, input length and scrollback. The engine library currently links Qt Widgets ([`engine/CMakeLists.txt:97`](/home/elliott/repos/relay-terminal/engine/CMakeLists.txt:97)); a headless API does not mean the current library is QtCore-only. Decide whether to compile narrow sources directly or accept existing linkage during a pilot.

For strictly stdlib Python, a bounded operation generator inside `unittest` can save the exact operation list and perform deletion-based reduction on failure. This is a useful small harness, not a claim of equivalence to Hypothesis's generation or shrinking. Keep test dependencies distinct from runtime dependencies when discussing Hypothesis adoption.

### Gaps

No harness was compiled or run. Instrumentation compatibility, event reset, memory limits, useful budgets and effective corpus coverage need measurement. Neither tool availability nor speed on this workstation was verified. Existing background discussion is at [`docs/research/qa-across-fields/b-systems-without-users.md:642`](/home/elliott/repos/relay-terminal/docs/research/qa-across-fields/b-systems-without-users.md:642); its statement that an example database makes failures permanent should not substitute for committed, reviewed regression fixtures.

## 2. How should differential testing and fault injection handle uncertain oracles?

### Takeaway

A discrepancy or deliberately induced exception is evidence to investigate, not automatically an application defect. Differential testing needs an agreed semantic overlap; fault injection needs an explicit recovery or containment expectation.

### Cited Findings

- **Verified — differential testing:** Csmith's own repository identifies differential compiler testing as its oracle and says it generates C programs without undefined behavior. Avoiding undefined behavior is central to making cross-implementation comparisons meaningful. [Csmith project](https://github.com/csmith-project/csmith)
- **Verified local opportunity:** `availableVtCores()` returns libvterm and conditionally Ghostty; `createVtCore` creates either and returns null for unavailable choices ([`engine/core/VtCoreFactory.cpp:11`](/home/elliott/repos/relay-terminal/engine/core/VtCoreFactory.cpp:11)). The common interface documents stream, screen and cursor observations ([`engine/core/VtCore.h:58`](/home/elliott/repos/relay-terminal/engine/core/VtCore.h:58)). This verifies a comparison seam, not parity of all terminal behavior or availability of both in a built binary.
- **Verified — chaos engineering:** The authors define an experiment around a measurable steady state, a hypothesis, realistic disturbances and comparison of outcomes. Their principles discuss production fidelity and minimizing blast radius. Randomly killing processes without an observable hypothesis does not meet that experiment design. [Principles of Chaos Engineering](https://principlesofchaos.org/)
- **Verified — Python injection:** `unittest.mock` supports scoped patching, exception/sequence `side_effect`s and spec-constrained mocks; patching must occur where an object is looked up. [Python unittest.mock](https://docs.python.org/3/library/unittest.mock.html)
- **Verified local integration seam:** Tests for the Tests protocol create a fake executable `ctest`, a real one-file unittest module and a real temporary board. The shim includes passing, failing and sleeping processes and records its invocations ([`tests/test_tests_protocol.py:4`](/home/elliott/repos/relay-terminal/tests/test_tests_protocol.py:4), [`tests/test_tests_protocol.py:46`](/home/elliott/repos/relay-terminal/tests/test_tests_protocol.py:46)). This supports testing process-boundary failure scenarios without live providers.

### Inferences

**Recommended differential pilot:** Generate a restricted common terminal language: printable ASCII, basic cursor positioning, erase, newline and a small agreed attribute subset. Run both cores at equal dimensions and compare normalized cell contents/cursor. Exclude unsupported extensions, device-identification replies and ambiguous terminal behavior initially. Reduce disagreements; then adjudicate against the contract or an independently reviewed expected result. Do not designate Ghostty or libvterm infallible. Shared wrapper errors can make both agree incorrectly; a previous-version oracle can preserve an old defect. Compare attributes explicitly if they matter: equal screen text misses styling errors. Qualitative cost is roughly the work of multiple executions plus potentially substantial mismatch triage, not an asserted runtime multiplier.

**Recommended smallest fault-injection harness:** Extend the conceptual fake-child pattern with deterministic cases: truncated output, nonzero exit before result creation, malformed result artifact, child ignoring graceful termination, and slow output. Specify outcomes before injection: one terminal run state, bounded stop, no orphan child, usable subsequent run, and retained diagnostic evidence. Treat each as a candidate requirement to confirm, not a claim that current code already guarantees it.

For file-writing code, scoped exceptions at open/write/replace boundaries can test error handling in a temporary project. Confirm the real writer's commit boundary before claiming an interrupted write must leave either old or new complete content. Mocked disk-full behavior checks the handler but cannot prove real filesystem durability. Subprocess termination checks lifecycle behavior but cannot reproduce all machine-crash semantics.

Prefer deterministic fault injection in isolated test processes before a broader chaos campaign. Chaos would become useful when there are multiple interacting workers, reconnects and resource failures to study against user-visible outcomes. GUI recovery checks can run under the existing offscreen convention; a GUI process surviving alone is weaker evidence than the session returning to a usable state. Schedule exploration and dependency simulation are separate investments, not something achieved by inserting random sleeps.

**Recommended finding classifications:** reproducible application failure; unadjudicated differential discrepancy; invalid or uncertain oracle; environment/harness failure. An agent can propose and reduce a case, but a runner should preserve the trace, expectation, actual observation and rerun. An empty findings list means nothing was found within the recorded target and budget, not that the project is correct.

### Gaps

No complete common terminal specification was established, and Ghostty build availability was not checked. Fault boundaries and recovery requirements need agreement with the owners of the relevant modules. No live process or network fault was injected. Cost, flake rate and triage burden are unmeasured.

## 3. Where does mutation testing fit, including per-change use and Python constraints?

### Takeaway

Mutation testing primarily probes whether tests detect deliberate changes to application code. It is not the same as finding a failing input for the unmodified application, and it is not inherently too slow for per-change use: scoped, incremental runs exist, but Relay's actual latency must be measured.

### Cited Findings

- **Verified — changed-line mutation:** Mull supports restricting mutations to source lines in a Git diff using `gitDiffRef` and `gitProjectRoot`. Its documentation explicitly contrasts this with full-project mutation's potentially large execution and analysis costs. This directly contradicts a categorical claim that mutation must be periodic rather than per-change. [Mull incremental mutation testing](https://mull.readthedocs.io/en/latest/IncrementalMutationTesting.html)
- **Verified — C++ control:** Mull's tutorial demonstrates limiting generated/executed mutants using configuration, excluding sources and selecting operators. The setup uses its compiler instrumentation and runner; it is not just another assertion library. Some filtering can occur at runner time without recompilation. [Mull mutation-control tutorial](https://mull.readthedocs.io/en/latest/tutorials/ControlMutationsTutorial.html)
- **Verified — stdlib runner option:** Cosmic Ray's tutorial mutates a specified Python module and runs `python -m unittest test_mod.py` via configurable `test-command`; it supports a per-test-command timeout and baseline execution. The mutation tool is an extra dependency, but does not require replacing the project's unittest runner. [Cosmic Ray tutorial](https://cosmic-ray.readthedocs.io/en/latest/tutorials/intro/)
- **Verified — alternative with a constraint:** Current mutmut documentation says it runs pytest, supports fork-based execution, remembers results, chooses relevant tests and accepts function/module selection patterns. This is a tooling mismatch if “no pytest” is a hard requirement. Its documented selection and incremental facilities are additional evidence against treating every mutation run as a complete suite over every mutant. [mutmut documentation](https://mutmut.readthedocs.io/en/latest/)

### Inferences

**Recommended distinctions for the Tests page:**

| Activity | What changes | What a result means |
|---|---|---|
| Application attack | Inputs, action sequence or operating conditions | Original program violated a stated expectation under a concrete scenario. |
| Mutation audit | Application implementation temporarily changed; tests retained | Tests did or did not detect that particular artificial change. |
| Test improvement | Assertions, fixtures or generators changed after reviewing evidence | New test should detect a meaningful fault without rejecting legitimate behavior. |

A surviving mutant is a prompt to inspect tests and relevance, not proof of a production bug. A mutant can preserve all permitted behavior (equivalent), fall outside the chosen tests' scope, alter intentionally unspecified behavior, or expose a weak assertion. Conversely, a “kill” due to unrelated flakiness or broken test infrastructure does not demonstrate a useful behavioral test. Report killed, survived, timeout, invalid and not-exercised outcomes separately where the tool distinguishes them; never silently turn a tool error into a successful kill. Define score denominator and exclusions before comparing scores. Avoid a universal score gate.

**Recommended smallest C++ pilot:** Select one already fast, deterministic Qt Test target and one production function; build its exercised code with a compatible Mull/Clang toolchain, exclude tests/vendor/generated sources, and begin with a small operator set such as comparison/boolean changes. Check that a known meaningful injected change is detected. Then compare changed-line and whole-function scope. Qt function/data-tag selection can narrow the executable's test run, but selection must still cover the changed behavior. Toolchain compatibility and instrumented build cost may outweigh execution on a tiny target; no support for this exact build was established.

**Recommended smallest Python pilot:** Cosmic Ray over one pure helper module and its existing focused unittest module; run a clean baseline, cap each mutant command, inspect a few survivors and promote useful missing cases into normal tests. This preserves the project runner. If even development dependencies must remain stdlib-only, a manually selected temporary source mutation plus existing unittest command can demonstrate the idea, but building a homegrown general mutation engine is poor first scope.

All mutation runs should operate on a disposable source snapshot, not this shared live checkout. The request prohibits implementation here; this is a proposed future execution boundary. Retain source revision/content hashes so a mutant cannot be attributed to a different concurrent edit.

**Proposed scheduling:** replay minimized failure fixtures and ordinary targeted tests per change; consider time-budgeted changed-line mutation and narrow fuzz searches when their targets change; reserve larger searches and mutation audits for on-demand/background runs. Changed-line selection can miss interactions with untouched callers or state, so it complements broader sampling. Measure build time, baseline time, mutants attempted/completed, timeout rate and human triage effort before choosing defaults. No benchmark or fixed cost ranking is asserted.

For card #SJTR, keep mutation results in a test-strength category even if the same QA agent orchestrates them. A confirmed surviving meaningful mutant can lead to a “test gap” card; a minimized failure against unchanged production code can lead to an application bug card. Both need evidence, but they answer different questions.

### Gaps

Mull, Cosmic Ray and mutmut were researched, not installed or trialed. Exact supported compiler/Python versions and packaging must be checked for the selected releases before adoption. The 12 linked primary sources above establish method/tool capabilities; they do not establish Relay throughput, bug yield, adoption effort or a suitable blocking threshold. Those remain pilot questions rather than reasons to reject mutation categorically.
