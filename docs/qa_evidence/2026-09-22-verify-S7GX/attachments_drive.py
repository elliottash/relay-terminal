"""Real localhost reads through session_protocol attachment entrypoint."""
import base64,hashlib,json,socket,subprocess,tempfile,time
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from relay_core import attachments,provider,session_protocol
from tests.test_guest_board_bridge import BridgeTests
OUT=Path(__file__).resolve().parent
rows=[]
def record(name,value):
    rows.append({'check':name,'result':value});print(name,json.dumps(value),flush=True)
def refusal(name,fn,fragment):
    try:fn()
    except ValueError as exc:
        assert fragment in str(exc),(fragment,str(exc));record(name,str(exc))
    else:raise AssertionError(name+' unexpectedly allowed')
with tempfile.TemporaryDirectory(prefix='s7cx-verify-') as tmp:
    root=Path(tmp);local=root/'local';remote=root/'remote';local.mkdir();remote.mkdir();ctl=root/'ctl'
    (local/'same.txt').write_text('LOCAL MUST NOT LEAK');(remote/'same.txt').write_text('REMOTE ONLY')
    (local/'missing.txt').write_text('LOCAL MUST NOT LEAK')
    png=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=')
    (remote/'pixel.png').write_bytes(png)
    with (remote/'too-large.png').open('wb') as f:
        f.write(png);f.truncate(provider.MAX_IMAGE_BYTES+1)
    (remote/'large.txt').write_bytes(b'x'*(attachments.PER_FILE_CAP+13))
    (remote/'binary').write_bytes(b'a\0b');(remote/'directory').mkdir()
    subprocess.run(['ssh','-M','-S',str(ctl),'-o','ControlPersist=no','-o','BatchMode=yes','-fNT','localhost'],check=True,capture_output=True,timeout=10)
    case=BridgeTests();case.setUp();case.active()
    try:
        session={'host':'localhost','control_path':str(ctl),'cwd':str(remote),'reachable':True}
        ex=SimpleNamespace(workspace=SimpleNamespace(root=local),remote_session=session)
        turns=SimpleNamespace(agent=SimpleNamespace(executor=ex))
        def load(path,**kwargs):
            return session_protocol.load_attachments({'attachments':[{'path':path,'host':'localhost',**kwargs}]},turns)[0]
        result=load('same.txt');assert result['content']=='REMOTE ONLY';assert result['path']==f'localhost:{remote}/same.txt';record('collision',result)
        result=attachments.load([{'path':'same.txt'}],local,remote_session=session)[0];assert result['content']=='LOCAL MUST NOT LEAK';record('explicit_local',result)
        result=load('pixel.png');assert result['raw']==png and result['kind']=='image';record('remote_image',{k:v for k,v in result.items() if k!='raw'})
        result=load('large.txt');assert result['truncated'] and len(result['content'])==attachments.PER_FILE_CAP;record('text_truncated',{k:v for k,v in result.items() if k!='content'})
        refusal('image_too_large',lambda:load('too-large.png'),'too large')
        refusal('remote_missing_local_exists',lambda:load('missing.txt'),'No such file')
        refusal('binary',lambda:load('binary'),'binary')
        refusal('wrong_host',lambda:load('same.txt',host='other'),'not the host')
        for value,fragment in [(None,'not logged'),({**session,'reachable':False},"can't be shared")]:
            ex.remote_session=value;refusal('unavailable',lambda:load('same.txt'),fragment)
        ex.remote_session=session
        # Independent race injections after genuine tool preparation, before execution.
        case.agent.executor.set_remote_session(session)
        original=case.agent._prepare
        def change(*args,**kw):
            prepared=original(*args,**kw);case.agent.executor.set_remote_session(None);return prepared
        with patch.object(case.agent,'_prepare',side_effect=change),patch.object(case.agent,'_execute') as execute:
            result=case.call('run_command',{'host':'localhost','command':'touch MUST_NOT_EXIST'},key='race-session')
            assert 'session changed' in result['error'];execute.assert_not_called();record('bridge_session_race',result)
        case.agent.executor.set_remote_session(session)
        def cancel(*args,**kw):
            prepared=original(*args,**kw);case.cancel.set();return prepared
        with patch.object(case.agent,'_prepare',side_effect=cancel),patch.object(case.agent,'_execute') as execute:
            result=case.call('run_command',{'host':'localhost','command':'touch MUST_NOT_EXIST'},key='race-cancel')
            assert 'cancelled' in result['error'];execute.assert_not_called();record('bridge_cancel_race',result)
        subprocess.run(['ssh','-S',str(ctl),'-O','exit','localhost'],check=True,capture_output=True)
        for _ in range(50):
            if not ctl.exists():break
            time.sleep(.02)
        refusal('socket_removed',lambda:load('same.txt'),'has closed')
        dead=socket.socket(socket.AF_UNIX);dead.bind(str(ctl));dead.close()
        refusal('socket_abandoned',lambda:load('same.txt'),'255')
        record('passed',True)
    finally:
        case.agent.executor.shutdown();case.doCleanups()
        subprocess.run(['ssh','-S',str(ctl),'-O','exit','localhost'],capture_output=True)
        repo=OUT.parents[2]
        paths=['backend/relay_core/attachments.py','backend/relay_core/session_protocol.py','backend/worker.py','tests/test_attachments.py']
        record('source_sha256',{p:hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in paths})
        (OUT/'attachments_results.json').write_text(json.dumps(rows,indent=2)+'\n')
