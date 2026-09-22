"""Real worker/protocol/providers; deterministic transport and guest harness boundaries only."""
import io, json, os, sys, urllib.error
from pathlib import Path
ROOT = Path(os.environ['MSW_ROOT'])
sys.path[:0] = [str(ROOT/'backend'), str(ROOT/'tests')]
from relay_core import guest_harness_provider as ghp, roles, keystore, provider, openrouter_catalog, relay_pro
from relay_core.guest_harness import HarnessError
from guest_harness_fake import FakeHarness, ev

def record(**data):
    with open(os.environ['MSW_TRANSPORT'], 'a') as out: out.write(json.dumps(data)+'\n')

class Harness(FakeHarness):
    def start(self, **kwargs):
        record(action='guest_start', model=kwargs.get('model'))
        if kwargs.get('model') == 'gpt-5.6-sol': raise HarnessError('Fixture guest startup refused')
        return super().start(**kwargs)
    def set_model(self, model):
        record(action='guest_model', model=model)
        if model == 'gpt-5.6-sol': raise HarnessError('Fixture model refused')
        return super().set_model(model)

ghp.make_harness = lambda guest_id: Harness([{'events':[ev('delta',text='Guest fixture answered.')],
    'result':('Guest fixture answered.','end',{})}]*20, guest=guest_id, model='gpt-6-astra')
ghp.adapter_available = lambda *a,**kw: True
ghp.installations = lambda: {'codex':{'installed':True,'binary':'/fixture/codex','version':'fixture'}}
ghp.login_status = lambda *a: True
ghp.start_catalog_scan = lambda *a,**kw: None
ghp.preset_rows = lambda: [dict(id='guest:codex',label='Codex',provider='codex',harness=True,
    model='',logged_in=True,installed=True,models=[dict(id=m,name=m,label=m,efforts=['low','medium','high','xhigh'])
    for m in ('gpt-6-astra','gpt-5.6-sol')])]
roles.guest_runnable = lambda guest: True
keystore.lookup = lambda *a,**kw: 'fixture-key'
openrouter_catalog.start_refresh = lambda *a,**kw: None
relay_pro.start_refresh = lambda *a,**kw: None

class Response(io.BytesIO):
    headers={'Content-Type':'text/event-stream'}
    status=200
class Opener:
    def open(self, request, timeout=None):
        body=json.loads(request.data)
        model=body['model']
        record(action='http',model=model)
        if model=='glm-5.3':
            data={'error':{'code':'1310','message':'Weekly/Monthly Limit Exhausted. Your limit will reset at 2026-09-23 05:53:09'}}
            raise urllib.error.HTTPError(request.full_url,429,'quota',{},io.BytesIO(json.dumps(data).encode()))
        if model=='meta/muse-spark-1.3':
            raise urllib.error.HTTPError(request.full_url,401,'fixture key refused',{},io.BytesIO(b'{}'))
        text='API fixture answered on '+model+'.'
        chunks=[{'choices':[{'delta':{'content':text},'finish_reason':None}]},
                {'choices':[{'delta':{},'finish_reason':'stop'}]}]
        return Response((''.join('data: '+json.dumps(c)+'\n\n' for c in chunks)+'data: [DONE]\n\n').encode())
provider.shared_opener=lambda **kw: Opener()
import worker
worker.main()
