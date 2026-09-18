"""Which lines get Relay's note ("syntax error: …", "command not found: …") under the agent echo.

Run from the repo root:
  RELAY_KEYRING=off PYTHONPATH=backend python3 docs/qa_evidence/2026-09-18-syntax-error-under-a-sentence/corpus.py
implementer-before.txt is this script against router.py at 790fc76, implementer-after.txt against
the current router. PROSE and HELDOUT_PROSE must print no NOTE, COMMAND and HELDOUT_COMMAND must keep theirs, SHELL must stay in the
shell. Results depend on this machine's PATH (the typo test asks it). Columns: route, NOTE when the
GUI would print invalid_reason under the line, the line, the note.
"""
import os, tempfile
from relay_core.router import classify
PROSE = [
 # the owner's report, and the same shape
 "check my provider (glm), am i out of credits or rate limited",
 "check my provider (glm)", "the build is broken (again)", "ok (I think) that works",
 "(sorry, wrong window)", "try the other provider (kimi or openrouter) and compare the answers",
 "is the key (the glm one) still valid?", "rename the tiers (Main, Flash, Lite) everywhere",
 "test the provider (glm) and tell me what it says", "that didn't work (same error as before)",
 "thanks :)", "hmm, not great :(",
 # quotes used the way writing uses them
 'the "fast" tier is slow', 'rename "Fast" to "Flash" in the settings pane', '"yeah" is "fine"',
 "the 'main' model keeps timing out", 'it says "rate limited", is that glm or us?',
 "the users' settings are gone", "the agent said 'done' but nothing changed",
 # markdown code spans in a sentence
 "run `make test` and fix what fails", "the `ls -la | wc -l` count looks wrong to me",
 "i ran `git status` and it shows nothing, is that right", "rename `foo` to `bar` in the router",
 # more than one line
 "ok two things\nfirst the provider (glm) is slow\nthen the note under my message is wrong",
 "yeah do that\nand then commit it", "a few notes:\n- the pane is too narrow\n- the font is wrong",
 "three things:\n1. the pane is too narrow\n2. the font (mono) is wrong\n3. it's slow",
 # a function word first, which sits one edit from some program (the/tee, not/nl, am/at)
 "the other one", "not sure", "am i out of credits", "am i out of credits/rate limited",
 "so the and/or case is broken", "that one e.g. the glm key", "my provider is down",
 "i think the provider is down; check the logs (if there are any)",
]
COMMAND = [
 "echo 'unfinished", 'echo "unfinished', "echo (hello)", "ls (foo)", "(cd /tmp && ls", "(ls",
 "for i in 1 2; do echo $i", "if true; then ls", "ls | | grep x", "cat foo >", "f() { ls",
 "gti commit -m \"fix the thing\"", 'grpe "foo bar" file.txt', "`gti status`", "frob (glm)",
 "gti status\nls", "ls\nnope123", "cd /tmp\nmkae", "xyzzy\nfrob", "is", "ls -la | grpe x",
 "gti log `date`", "echo $(", "x=(1 2", "while true; do sleep 1", "case x in", "ls &&",
 "pyton -c 'print(1)'", "docekr run --rm (x)",
]
SHELL = ["echo hi; ls", "(cd /tmp && ls)", "echo 'the (glm) provider'", "f() { ls; }; f", "echo `date`",
         'echo "rate limited (again)"', "ls\npwd"]
# Held out: written after the detector, to see what it missed. First run 26 of 30 and 19 of 20
# sentences quiet (a leading number, "error:" with no function word, " & ", "->", "yes (both)"),
# all since fixed; every broken command kept its note both times.
HELDOUT_PROSE = [
 'for now, skip the slow tests (they take ages)',
 'if not, lets unblock (and tell me)',
 'then the note is wrong (see above)',
 'while you are at it, fix the hint (the ctrl+i one)',
 'also: the pane (left one) flickers',
 'hmm (not sure about that)',
 'which model is this (main or flash)?',
 'who wrote this (git blame?)',
 'sounds good (ship it)',
 "Commit it (but don't push)",
 'push it; then tell me (briefly) what changed',
 'the error was "429 too many requests" (from glm)',
 'error: rate limited (code 1302)',
 "openrouter says 'insufficient credits', can you check",
 'my key is fine, right? (the kimi one)',
 "I'd like a summary (short) of today's commits",
 '2 things: first the font, second the (broken) tabs',
 'ok so:\n\n1) the router is wrong\n2) the hint is wrong too',
 'no wait; use the other branch (main)',
 'glm is down? (or just slow)',
 'we need to talk about the settings pane (again...)',
 'make it faster (please)',
 "kill the old worker (if it's still there) and restart",
 'look at src/Pane.h (around line 5700) and tell me what it does',
 'bonsai (the local model) keeps stalling',
 'did that work? (y/n)',
 'same as before (but with kimi this time)',
 'nvm (figured it out)',
 "commit & push when you're done (no force)",
 'use a -> b style arrows (not =>)',
 'quick q (sorry): does kimi support tools?',
 'yes (both)',
 'no (neither)',
 'option 2 (the cheaper one)',
 'B (but keep the old name)',
 'thanks (again) :)',
 'the tests pass now (all 70), commit it',
 'ugh, glm again (rate limit?)',
 'try again (it timed out)',
 'retry (it timed out)',
 'deploy (staging only)',
 'status (of the build)?',
 'ok... (sigh) do it',
 'hm, "command not found" again',
 'Works! (finally)',
 'it printed `syntax error near unexpected token` under my message',
 'see above (the screenshot)',
 'lgtm (one nit: rename x)',
 'the file (src/Pane.h) is huge',
 'fine; whatever (you decide)',
]
HELDOUT_COMMAND = [
 "git commit -m 'wip",
 'grep -r "foo( src',
 'sudo apt install (foo)',
 'ssh host (',
 'cd (tmp)',
 'mkdir (new)',
 'python -c "print(\'hi\')',
 'gti push; gti status',
 'cat file.txt | grpe (x)',
 'make test (',
 'ls; (pwd',
 'for f in *.py; do echo $f',
 'function foo { ls',
 '[[ -f x ]',
 'echo hi >> (out)',
 'kubectl get pods (prod)',
 'npm run build (',
 'docker ps\ndocekr logs x',
 'export FOO=(bar',
 'pip install foo (bar)',
 'gti (status)',
 'sl (',
 'cd.. (',
 'grpe (foo) bar.txt',
 'mkae (all)',
 'sudo (ls)',
 'ls (-la)',
 'echo hi (there)',
 'printf (x)',
 'git log (HEAD)',
 'cat (a.txt)',
 'rm (old)',
 './run.sh (fast)',
]
with tempfile.TemporaryDirectory() as d:
  for label, corpus in (("PROSE", PROSE), ("COMMAND", COMMAND), ("SHELL", SHELL),
                        ("HELDOUT_PROSE", HELDOUT_PROSE), ("HELDOUT_COMMAND", HELDOUT_COMMAND)):
    print("==", label)
    for t in corpus:
        r = classify(t, path=os.environ["PATH"], cwd=d)
        note = r.invalid_reason if (r.route == "agent" and r.explain_invalid and r.invalid_reason) else ""
        print(f"{r.route:6} {'NOTE' if note else '    '} {t!r:90} {note}")
