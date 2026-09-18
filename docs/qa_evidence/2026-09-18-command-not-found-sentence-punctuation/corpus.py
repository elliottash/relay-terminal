"""Card #W954 evidence: which lines get Relay's "command not found" note under the agent echo.

Run from the repo root:  RELAY_KEYRING=off PYTHONPATH=backend python3 docs/qa_evidence/2026-09-18-command-not-found-sentence-punctuation/corpus.py
implementer-before.txt is this script against router.py at f0cd78f, round2-before.txt against
54f4c0c (the first fix, before the `;`, quoted-first-word and lone `wait`/`true`/`false`/`done`
cases), implementer-after.txt against the current router. To reproduce a "before", put that
commit's router.py alone in a scratch relay_core/ package and run with PYTHONPATH pointing there.
The SHELL section must stay in the shell in every column. Columns: route, NOTE when the GUI would print invalid_reason under the line, the line, the
note. Results depend on this machine's PATH (the typo test asks it).
"""
import os, sys, tempfile
from relay_core.router import classify
PROSE = [
 "yeah, see if there is a clear issue to resolve. if not, lets unblock and start backfilling at full capacity",
 "yeah see if there is a clear issue to resolve. if not, lets unblock and start backfilling at full capacity",
 "yeah,", "yeah.", "ok.", "ok, go ahead", "okay: do it", "hmm; not sure", "really?", "ready?", "done!", "thanks!",
 "great, thanks", "Yeah, see if there is a clear issue", "Yeah", "Sure", "Hmm, what now",
 "nope, try again", "cool.", "right...", "wait... what", "ok...", "hmm…", "yes!", "sure thing.",
 "let's unblock and start backfilling", "don't break the build", "lets go", "it's broken", "what's up", "Let's go",
 "yeah — do it", "yeah—do it", "ok -- do it", "yeah - do it", "nah, leave it", "\"yeah\" is fine",
 "yep: ship it", "sounds good.", "Sounds good!", "perfect, now commit", "alright, carry on",
 "hmm?", "ok!", "yeah, thanks!", "awesome", "Awesome!", "Continue", "Resume", "resume.", "continue.",
 "ok", "no", "good", "fine", "cool", "nope", "yep", "hi", "go", "stop", "yes", "Yes", "nice", "wait", "Wait", "really?!", "it's fine, isn't it", "don’t break it", "ok go", "Great, thanks.", "“yeah” works",
 # round 2: semicolons in a sentence, a quoted first word, lone builtins that are replies
 "hmm;", "ok; thanks", "no; the other one", "nope; try again", "ok; let me think", "Hmm; no idea",
 "hmm; don't know", "'ok' then", "\"yeah\", sure", "true", "false", "done",
]
TYPO = ["gti status", "pyton -m x", "./run.sh", "ls -la | grpe x", "nonexistentcmd123", "docekr ps -a",
        "pyton script.py", "lls", "ls | nonexistentcmd123", "gti", "Gti status", "kubectl2 get pods", "pip4 install x",
        "gti, status", "echo 'unfinished", "echo \"unfinished", "Docker ps", "gti stauts.", "GTI status", "Ls", "gti --", "yes | head -3", "Gti", "pyton,",
        # round 2
        "hmm; ls", "gti; ls", "cd /tmp; mkae", "ok; Docker ps", "xyzzy; frob", "\"gti\" status", "\"./run.sh\"",
        "\"yeah\" is \"fine\""]
SHELL = ["echo hi; ls", "\"ls\" -la", "wait %1", "wait $!", "true && ls", "ls", "pwd", "clear", "history",
         "jobs", "times", "nice -n 10 ls"]
with tempfile.TemporaryDirectory() as d:
  for label, corpus in (("PROSE", PROSE), ("TYPO", TYPO), ("SHELL", SHELL)):
    print("==", label)
    for t in corpus:
        r = classify(t, path=os.environ["PATH"], cwd=d)
        note = r.invalid_reason if (r.route == "agent" and r.explain_invalid and r.invalid_reason) else ""
        print(f"{r.route:6} {'NOTE' if note else '    '} {t!r:60} {note}")
