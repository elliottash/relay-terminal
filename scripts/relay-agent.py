#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnostic CLI for the same agent backend; not a replacement for the rich GUI."""
import argparse
import getpass
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from relay_core import keystore, localmodels, skills as skills_index
from relay_core.agent import Agent
from relay_core.presets import PRESETS
from relay_core.session_protocol import provider_config

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace', default=str(Path.cwd()))
parser.add_argument('--provider', default='kimi',
                    help="a preset (%s), 'custom', or a model server on this machine saved with "
                         "scripts/relay-local.py, as local:<id>" % ', '.join(PRESETS))
parser.add_argument('--base-url')
parser.add_argument('--model')
parser.add_argument('--import-warp', action='store_true',
                    help="copy Warp's custom-endpoint keys into Relay's keyring entries and exit")
parser.add_argument('--save-key', action='store_true', help='save an entered key to the keyring')
parser.add_argument('--list', action='store_true', help='list presets and whether a key is stored')
parser.add_argument('--prompt', help='ask this once and exit, instead of reading prompts from the terminal')
parser.add_argument('--yes', action='store_true', help='skip the confirmation (for scripted checks)')
parser.add_argument('--no-skills', action='store_true',
                    help='do not offer the user and bundled skills (load_skill / read_skill_file)')
args = parser.parse_args()
local = localmodels.find(args.provider)
if args.provider not in PRESETS and args.provider != 'custom' and local is None:
    known = ', '.join([*PRESETS, 'custom', *localmodels.catalog()])
    sys.exit(f'Unknown provider {args.provider!r}. Known: {known}')

if args.import_warp:
    try:
        imported, skipped = keystore.import_from_warp()
    except keystore.KeystoreError as exc:
        sys.exit(f'Import failed: {exc}')
    for item in imported:
        print(f'Imported {item.name} ({item.model}) -> preset {item.preset}')
    for item in skipped:
        print(f'Skipped {item}')
    sys.exit(0 if imported else 1)
if args.list:
    stored = keystore.available()
    for preset in PRESETS.values():
        print(f"{preset.id:12} {preset.model:32} {'key stored' if stored[preset.id] else 'no key'}  {preset.base_url}")
    for endpoint in localmodels.catalog().values():
        print(f"{endpoint.id:12} {endpoint.model:32} {'no key needed':10}  {endpoint.base_url}")
    sys.exit(0)

if local is not None:
    base, model, extra = local.base_url, local.model, dict(local.extra)
elif args.provider == 'custom':
    base, model, extra = 'http://127.0.0.1:11434/v1', '', {}
else:
    preset = PRESETS[args.provider]
    base, model, extra = preset.base_url, preset.model, dict(preset.extra)
base, model = args.base_url or base, args.model or model
print(f'Provider: {base}\nModel: {model}\nWorkspace: {Path(args.workspace).resolve()}')
print('Submitted prompts and tool results go to this provider. Tools run WITHOUT confirmation and shell commands are NOT sandboxed.')
if not args.yes and input('Continue? [y/N] ').strip().lower() != 'y': sys.exit(0)
keyless = localmodels.keyless(args.provider, base)       # plain HTTP to this machine: there is no key
key = keystore.lookup(args.provider) if args.provider in PRESETS else ''
if key:
    print(f'Using stored key for {args.provider}.')
elif keyless:
    print('Local model server: no key is needed or sent.')
else:
    key = getpass.getpass('API key (memory only; empty for local server): ')
    if args.save_key and key and args.provider != 'custom':
        keystore.store(args.provider, key)
        print('Key saved to keyring.')
# The same funnel the worker uses, so a local endpoint gets its window, deadlines and clamped output.
config = provider_config({'preset': args.provider, 'base_url': base, 'model': model, 'extra': extra, 'api_key': key})

def emit(event):
    kind = event['event']
    if kind in {'delta','tool_output'}:
        print(event.get('text',''), end='', flush=True)
    elif kind == 'tool_started':
        print('\n\n[running] ' + event.get('preview', event['tool'])[:4000])
    elif kind == 'tool_result':
        result = event['result']
        print('\n[tool result]', result.get('error') or 'completed')
    elif kind in {'error','status','done','cancelled'}:
        print('\n[' + kind + '] ' + event.get('text',''))

# The same skill index a pane gets (the user's directories plus Relay's bundled ones), so a skill
# can be checked here without the GUI.
index = None if args.no_skills else skills_index.from_request(None, args.workspace)
if index is not None and index.skills:
    print(f'Skills: {len(index.skills)} available ({", ".join(list(index.skills)[:6])}…)')
agent = Agent(config, args.workspace, emit, skills=index,
              preset_id=args.provider if args.provider != 'custom' else None)
if args.prompt:
    agent.ask(args.prompt)
    print()
    sys.exit(0)
try:
    while True:
        prompt = input('\nrelay-agent> ')
        if prompt.strip() in {'/quit','/exit'}: break
        if prompt.strip() == '/new': agent.messages = agent.messages[:1]; continue
        if prompt.strip(): agent.ask(prompt)
except (KeyboardInterrupt, EOFError):
    agent.stop()
    print('\nStopped. Completed actions have not been rolled back.')
