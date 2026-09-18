#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Model servers on this machine: probe one, and keep the list Relay offers in its model dropdown.

  relay-local.py probe [URL]            what is serving there, its models and its real window
  relay-local.py scan                   probe the usual ports (Ollama, LM Studio, llama.cpp, vLLM)
  relay-local.py list                   the saved endpoints
  relay-local.py add --base-url URL     save one; --detect fills model, server and window
  relay-local.py remove ID
  relay-local.py unit                   print a systemd user unit for llama-server (installs nothing)

The registry is $XDG_CONFIG_HOME/relay/local-models.json, the file the worker reads. Nothing here
starts a server or talks to anything but a loopback address. See docs/LOCAL-MODELS.md.
"""
import argparse
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from relay_core import localmodels

UNIT = """\
# ~/.config/systemd/user/llama-server@.service
# One instance per model: `systemctl --user start llama-server@bonsai` reads
# ~/.config/relay/llama-server/bonsai.env, which sets LLAMA_SERVER and LLAMA_ARGS, for example
#   LLAMA_SERVER=/opt/llama.cpp/build/bin/llama-server
#   LLAMA_ARGS=-m /models/model.gguf -ngl 99 -fa on -c 65536 --jinja --reasoning-format deepseek \\
#              -np 1 --host 127.0.0.1 --port 8080 --alias my-model --sleep-idle-seconds 600
# --jinja is what makes tool calls come back as tool_calls. --sleep-idle-seconds frees the model
# after ten idle minutes and reloads it on the next request; leave the unit disabled at boot.
[Unit]
Description=llama-server (%i) for Relay

[Service]
EnvironmentFile=%h/.config/relay/llama-server/%i.env
ExecStart=/bin/sh -c 'exec "$LLAMA_SERVER" $LLAMA_ARGS'
Restart=on-failure
RestartSec=5
# A model server is the last thing the OOM killer should pick while a build is running.
OOMScoreAdjust=-100

[Install]
WantedBy=default.target
"""


def show(found) -> None:
    if not found.ok:
        print(f'{found.base_url or "(no URL)"}: {found.error}')
        return
    label = localmodels.SERVER_LABELS.get(found.server, found.server)
    window = f', window {found.context_window:,}' if found.context_window else ''
    print(f'{found.base_url}: {label}, {found.state}{window}')
    for model in found.models:
        facts = [f'{model["context_window"]:,} tokens' if model.get('context_window') else 'window unknown']
        for name in ('tools', 'thinking'):
            if model.get(name) is not None:
                facts.append(name if model[name] else f'no {name}')
        print(f'  {model["id"]}  ({", ".join(facts)})')
    if found.error:
        print(f'  {found.error}')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    probe = sub.add_parser('probe')
    probe.add_argument('url', nargs='?', default='http://127.0.0.1:8080/v1')
    probe.add_argument('--json', action='store_true')
    sub.add_parser('scan')
    sub.add_parser('list').add_argument('--json', action='store_true')
    add = sub.add_parser('add')
    add.add_argument('--base-url', required=True)
    add.add_argument('--id')
    add.add_argument('--label')
    add.add_argument('--model')
    add.add_argument('--server', choices=localmodels.SERVERS)
    add.add_argument('--context-window', type=int)
    add.add_argument('--first-token-timeout', type=float)
    add.add_argument('--temperature', type=float)
    add.add_argument('--top-p', type=float)
    add.add_argument('--tool-text-recovery', action='store_true',
                     help='recover tool calls the model writes as text (off by default; see the docs)')
    add.add_argument('--detect', action='store_true', help='ask the server for model, kind and window')
    sub.add_parser('remove').add_argument('id')
    sub.add_parser('unit')
    args = parser.parse_args()

    if args.command == 'unit':
        print(UNIT, end='')
        return 0
    if args.command == 'probe':
        found = localmodels.probe(args.url)
        print(json.dumps(found.to_dict(), indent=2)) if args.json else show(found)
        return 0 if found.ok else 1
    if args.command == 'scan':
        up = 0
        for port in sorted(set(localmodels.DEFAULT_PORTS.values())):
            found = localmodels.probe(f'http://127.0.0.1:{port}')
            if found.ok:
                up += 1
                show(found)
        if not up:
            print('Nothing is serving on ' + ', '.join(str(p) for p in sorted(localmodels.DEFAULT_PORTS.values())) + '.')
        return 0 if up else 1
    if args.command == 'list':
        items = [e.to_dict() for e in localmodels.catalog().values()]
        if args.json:
            print(json.dumps(items, indent=2))
        for item in ([] if args.json else items):
            print(f'{item["id"]:24} {item["model"]:28} {item["context_window"]:>9,}  {item["base_url"]}')
        if not items and not args.json:
            print(f'No local endpoints saved ({localmodels.config_path()}).')
        return 0
    if args.command == 'remove':
        removed = localmodels.delete(localmodels.make_id(args.id))
        print('Removed.' if removed else 'No such endpoint.')
        return 0 if removed else 1
    # add
    spec = {key: value for key, value in {
        'id': args.id, 'label': args.label, 'base_url': args.base_url, 'model': args.model,
        'server': args.server, 'context_window': args.context_window,
        'first_token_timeout': args.first_token_timeout}.items() if value is not None}
    extra = {key: value for key, value in {'temperature': args.temperature, 'top_p': args.top_p}.items()
             if value is not None}
    if extra:
        spec['extra'] = extra
    if args.tool_text_recovery:
        spec['tool_text_recovery'] = True
    try:
        if args.detect:
            spec, found = localmodels.detect(spec)
            show(found)
            if not found.ok:
                return 1
        endpoint = localmodels.save(spec)
    except ValueError as exc:
        print(f'Not saved: {exc}', file=sys.stderr)
        return 1
    print(f'Saved {endpoint.id}: {endpoint.model} at {endpoint.base_url}, window {endpoint.context_window:,}. '
          f'Pick it in the model dropdown, or: scripts/relay-agent.py --provider {endpoint.id}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
