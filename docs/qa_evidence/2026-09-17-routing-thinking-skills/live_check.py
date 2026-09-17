import json, os, shutil, sys, tempfile, threading, time
from pathlib import Path
REPO = str(Path(__file__).resolve().parents[3])
sys.path.insert(0, REPO + '/backend')
from relay_core import keystore, route_assist, skill_manage, skills
from relay_core.agent import Agent
from relay_core.presets import PRESETS
from relay_core.provider import ProviderConfig

OUT = []
def log(obj):
    line = json.dumps(obj, ensure_ascii=False)
    print(line, flush=True)
    OUT.append(line)

EXAMPLES = ['go build ./...', 'go to the docs folder and summarize', 'make the tests pass', 'install ripgrep',
            'find where the config is loaded', 'kill it']
ws = tempfile.mkdtemp(prefix='relay-live-ws-')
which = sys.argv[1]

def agent_for(pid, max_tokens=8192):
    p = PRESETS[pid]
    cfg = ProviderConfig(p.base_url, p.model, keystore.lookup(pid), dict(p.extra), max_tokens)
    return Agent(cfg, ws, lambda e: None, preset_id=pid)

def assist(agent, text, timeout_ms, max_tokens=route_assist.MAX_TOKENS):
    events = []
    done = threading.Event()
    def emit(e):
        events.append(e); done.set()
    provider = agent.side_provider(cheap=True, max_tokens=max_tokens)
    route_assist.run(provider, {'id': text, 'text': text, 'cwd': ws, 'timeout_ms': timeout_ms}, emit)
    done.wait(timeout_ms / 1000 + 20)
    return events[0] if events else None

if which == 'assist':
    for pid in ('kimi', 'glm-coding'):
        agent = agent_for(pid)
        for timeout in (2000, 15000):
            for text in EXAMPLES:
                ev = assist(agent, text, timeout)
                log({'preset': pid, 'timeout_ms': timeout, 'max_tokens': route_assist.MAX_TOKENS, 'text': text,
                     **{k: ev.get(k) for k in ('route', 'confidence', 'reason', 'error', 'elapsed_ms')}})
        ev = assist(agent, 'install ripgrep', 15000, max_tokens=20)  # the spec's ~20
        log({'preset': pid, 'timeout_ms': 15000, 'max_tokens': 20, 'text': 'install ripgrep',
             **{k: ev.get(k) for k in ('route', 'confidence', 'reason', 'error', 'elapsed_ms')}})

if which == 'thinking':
    events = []
    p = PRESETS['kimi']
    cfg = ProviderConfig(p.base_url, p.model, keystore.lookup('kimi'), {'reasoning_effort': 'low'}, 2048)
    agent = Agent(cfg, ws, events.append, preset_id='kimi')
    agent.ask('In one sentence: what does `ls -la` show?', turn_id='live-1')
    kinds = [e['event'] for e in events]
    thinking = ''.join(e['text'] for e in events if e['event'] == 'thinking_delta')
    answer = ''.join(e['text'] for e in events if e['event'] == 'delta')
    log({'check': 'thinking', 'first_thinking_index': kinds.index('thinking_delta') if 'thinking_delta' in kinds else None,
         'thinking_deltas': kinds.count('thinking_delta'), 'thinking_done': [e for e in events if e['event'] == 'thinking_done'],
         'thinking_done_before_first_delta': kinds.index('thinking_done') < kinds.index('delta') if 'delta' in kinds and 'thinking_done' in kinds else None,
         'thinking_chars': len(thinking), 'answer': answer[:200], 'reasoning_in_delta': bool(thinking) and thinking[:40] in answer,
         'turn_summary': [e for e in events if e['event'] == 'turn_summary'], 'done': [e for e in events if e['event'] == 'done']})

if which == 'refine':
    source_root = Path(tempfile.mkdtemp(prefix='relay-live-skills-src-'))
    target = Path(tempfile.mkdtemp(prefix='relay-live-skills-target-'))
    shutil.copytree(Path.home() / '.warp/skills/clean-commit', source_root / 'clean-commit')
    original = (source_root / 'clean-commit/SKILL.md').read_text()
    listing = skill_manage.list_skills([source_root])
    agent = agent_for(sys.argv[2] if len(sys.argv) > 2 else 'kimi')
    started = time.monotonic()
    result = skill_manage.refine_one(agent.side_provider(), listing, 'clean-commit', target)
    refined = Path(result['path']).read_text()
    before, after = skills.parse_frontmatter(original), skills.parse_frontmatter(refined)
    log({'check': 'refine', 'preset': sys.argv[2] if len(sys.argv) > 2 else 'kimi', 'elapsed_s': round(time.monotonic() - started, 1),
         'result': result, 'original_bytes': len(original), 'refined_bytes': len(refined),
         'name_before': before.get('name'), 'name_after': after.get('name'),
         'description_before': before.get('description'), 'description_after': after.get('description'),
         'refined_from': after.get('refined_from'),
         'original_untouched': (source_root / 'clean-commit/SKILL.md').read_text() == original,
         'real_original_untouched': (Path.home() / '.warp/skills/clean-commit/SKILL.md').read_text() == original,
         'headings_after': [l for l in refined.splitlines() if l.startswith('#')][:15]})
    shutil.copy(result['path'], os.path.join(tempfile.gettempdir(), 'refined-clean-commit.md'))
