#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnostic CLI for the same agent backend; not a replacement for the rich GUI."""
import argparse
import getpass
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from relay_core import keystore
from relay_core.agent import Agent
from relay_core.presets import PRESETS
from relay_core.provider import ProviderConfig

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace', default=str(Path.cwd()))
parser.add_argument('--provider', choices=[*PRESETS, 'custom'], default='kimi')
parser.add_argument('--base-url')
parser.add_argument('--model')
parser.add_argument('--import-warp', action='store_true',
                    help="copy Warp's custom-endpoint keys into Relay's keyring entries and exit")
parser.add_argument('--save-key', action='store_true', help='save an entered key to the keyring')
parser.add_argument('--list', action='store_true', help='list presets and whether a key is stored')
args = parser.parse_args()

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
    sys.exit(0)

if args.provider == 'custom':
    base, model, extra = 'http://127.0.0.1:11434/v1', '', {}
else:
    preset = PRESETS[args.provider]
    base, model, extra = preset.base_url, preset.model, dict(preset.extra)
base, model = args.base_url or base, args.model or model
print(f'Provider: {base}\nModel: {model}\nWorkspace: {Path(args.workspace).resolve()}')
print('Submitted prompts and tool results go to this provider. Tools run WITHOUT confirmation and shell commands are NOT sandboxed.')
if input('Continue? [y/N] ').strip().lower() != 'y': sys.exit(0)
key = keystore.lookup(args.provider) if args.provider != 'custom' else ''
if key:
    print(f'Using stored key for {args.provider}.')
else:
    key = getpass.getpass('API key (memory only; empty for local server): ')
    if args.save_key and key and args.provider != 'custom':
        keystore.store(args.provider, key)
        print('Key saved to keyring.')
config = ProviderConfig(base, model, key, extra)
config.validate()

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

agent = Agent(config, args.workspace, emit)
try:
    while True:
        prompt = input('\nrelay-agent> ')
        if prompt.strip() in {'/quit','/exit'}: break
        if prompt.strip() == '/new': agent.messages = agent.messages[:1]; continue
        if prompt.strip(): agent.ask(prompt)
except (KeyboardInterrupt, EOFError):
    agent.stop()
    print('\nStopped. Completed actions have not been rolled back.')
