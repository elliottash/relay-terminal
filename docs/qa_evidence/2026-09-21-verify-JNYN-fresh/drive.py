"""Fresh #JNYN verification; only disposable fixtures are written."""
import sys, tempfile, json, subprocess, os, time, hashlib
from pathlib import Path
sys.path[:0]=['backend','tests']
from test_tryit_protocol import TryItTest
from relay_core import board as B
from relay_core.board_tools import section_text

def backend():
 t=TryItTest(); t.setUp()
 try:
  t.start(); t.finish(outcome='error',text='')
  t.start(); t.write_section('Open it: `true`\n\nLook at the result.\n\nWas it clear?\n')
  print('Retry after failure:',json.dumps(t.tryit_events(t.finish())))
  t.write_section('1. Existing owner decision?\n   Answer: Keep the old layout.\n',heading='Human QA')
  t.send(type='try_answer',card=t.card,answer='Verifier synthetic answer.')
  human=section_text(t.board.card_by_id(t.card).body,'Human QA')
  print('Existing Human QA after try_answer:',repr(human))
  print('Prior owner decision preserved:', 'Keep the old layout.' in human)
 finally: t.doCleanups()

def stage():
 root=Path(tempfile.mkdtemp(prefix='jnyfresh-')); (root/'issues/features').mkdir(parents=True)
 (root/'issues/board.yaml').write_text('version: 1\ntabs: [{id: features, folder: features}]\ncolumns: [inbox, executing, needs-verification, done]\nagent: {autonomy: auto, max_creates_per_turn: 5}\n')
 (root/'expected.md').write_text('SEALED-FRESH: the marker appears after Open it.\n')
 (root/'issues/features/fixture.md').write_text('---\nid: T9QA\ntype: work\nstatus: needs-verification\nlabels: [feature]\nrank: m\ncreated: \'2026-09-21\'\nlinks: {plans: [], commits: [], evidence: [], related: [], github: null}\n---\n# Fresh Try it verification\n\n## Issue\nVerify the handoff and sealed answer.\n\n## Done means\nOpen it prints a marker.\n\n## Try it\n1. Open it: `printf JNYN-FRESH-OPEN`\n\n2. The task: inspect the staged marker.\n\n3. Was this easy to understand?\n\nExpected: expected.md (sealed until you answer)\n')
 subprocess.run(['git','init','-q',str(root)],check=True)
 print(root)
if __name__=='__main__':
 {'backend':backend,'stage':stage}[sys.argv[1]]()
