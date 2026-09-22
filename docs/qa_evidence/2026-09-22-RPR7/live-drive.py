"""Isolated Relay + actual gateway + fake upstream. No production credentials.
Run from repo root with PYTHONPATH=.:backend. The control file accepts screenshot,
revoke and quit. Temporary keyring stand-in stores only a generated test code.
"""
import json, os, subprocess, tempfile, time
from pathlib import Path
from tests.test_gateway import FakeUpstream, Gateway, KEY_ENV, config_for

root = Path.cwd()
sandbox = Path(tempfile.mkdtemp(prefix='rpr7-live-'))
for name in ('config/RelayTerminal', 'data', 'cache', 'run', 'work', 'bin', 'evidence'):
    (sandbox/name).mkdir(parents=True, exist_ok=True)
(sandbox/'run').chmod(0o700)
os.environ[KEY_ENV] = 'upstream-secret'
upstream = FakeUpstream()
conf = config_for(upstream.base, tokens_per_day=10000000, registrations_per_ip=100)
for role, model, effort in [('relay-pro-high', 'glm-5.3', 'high'), ('relay-pro-main', 'glm-5.3', 'medium'),
                            ('relay-pro-flash', 'glm-5.3-flash', 'low')]:
    conf['roles'][role] = {'upstreams': [{'provider': 'ok', 'model': model}], 'effort': effort,
                           'max_effort': 'high', 'max_output_tokens': 300, 'max_input_chars': 1000000}
    conf['providers']['ok']['price_per_mtok'][model] = [0.1, 0.1] # synthetic fixture prices
for role in conf['roles'].values():
    role['max_input_chars'] = 1000000
conf['roles']['relay-flash'] = dict(conf['roles']['relay-main'])
gateway = Gateway(conf)
code = gateway.store.issue_pro_code('isolated-ui-person')
(sandbox/'test-code').write_text(code)
(sandbox/'test-code').chmod(0o600)
# Stand in for Secret Service, never reach the real desktop keyring.
(sandbox/'bin/secret-tool').write_text('''#!/usr/bin/python3
import os, sys
from pathlib import Path
p=Path(os.environ['QA_SANDBOX'])/('key-'+sys.argv[-1])
if sys.argv[1]=='store': p.write_text(sys.stdin.read()); p.chmod(0o600)
elif sys.argv[1]=='clear': p.unlink(missing_ok=True)
elif p.exists(): print(p.read_text())
else: sys.exit(1)
''')
(sandbox/'bin/secret-tool').chmod(0o700)
(sandbox/'config/RelayTerminal/relay.conf').write_text('''[instructions]
onboarded=true
[isolation]
enabled=false
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
[url_handler]
announced=true
''')
display = next(':'+str(n) for n in range(950,970) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
env = {k:v for k,v in os.environ.items() if not k.startswith(('RELAY_', 'ANTHROPIC_', 'OPENAI_', 'CODEX_'))}
env.update(HOME=str(sandbox), XDG_CONFIG_HOME=str(sandbox/'config'), XDG_DATA_HOME=str(sandbox/'data'),
           XDG_CACHE_HOME=str(sandbox/'cache'), XDG_RUNTIME_DIR=str(sandbox/'run'),
           PATH=str(sandbox/'bin')+':/usr/bin:/bin', DISPLAY=display, QA_SANDBOX=str(sandbox),
           RELAY_OPENROUTER_CATALOG='off', RELAY_HOSTED_URL=f'http://127.0.0.1:{gateway.port}/v1')
(sandbox/'env.json').write_text(json.dumps({k:env[k] for k in ('DISPLAY','QA_SANDBOX')}))
Path('/tmp/rpr7-live-path').write_text(str(sandbox))
xvfb = subprocess.Popen(['Xvfb', display, '-screen', '0', '1500x960x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(.5)
log = (sandbox/'evidence/relay.log').open('w')
relay = subprocess.Popen([str(root/'build/relay'), '--fresh', '--workspace', str(sandbox/'work')],
                         env=env, stdout=log, stderr=log)
print('sandbox', sandbox, 'display', display, 'pid', relay.pid, flush=True)
try:
    while relay.poll() is None:
        control = sandbox/'control'
        if control.exists():
            command = control.read_text().strip(); control.unlink()
            if command == 'quit': break
            if command == 'revoke':
                assert gateway.store.revoke_pro_code('isolated-ui-person')
                print('revoked test person', flush=True)
            elif command.startswith('shot '):
                subprocess.run(['import', '-window', 'root', str(sandbox/'evidence'/(command[5:]+'.png'))], env=env, check=True)
        # Only synthetic request bodies; deliberately omit all HTTP headers.
        (sandbox/'evidence/upstream.json').write_text(json.dumps(
            [{'path': item['path'], 'body': item['body']} for item in upstream.requests], indent=2))
        time.sleep(.25)
finally:
    relay.terminate()
    try: relay.wait(5)
    except subprocess.TimeoutExpired: relay.kill(); relay.wait()
    log.close(); xvfb.terminate(); xvfb.wait(); gateway.close(); upstream.stop()
    for file in (sandbox/'test-code', sandbox/'key-relay-pro'):
        file.unlink(missing_ok=True)
