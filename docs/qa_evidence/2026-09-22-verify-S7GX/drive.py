"""Independent S7GX: real MCP proxy/native executor/SSH; no model network calls."""
import hashlib,json,os,select,socket,subprocess,tempfile,threading,time
from pathlib import Path
from tests.test_guest_board_bridge import BridgeTests
from relay_core import guest_harness_provider as P, remote_session
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[2]
results=[]
def record(label,result):
    results.append({'check':label,'result':result})
    print(label,json.dumps(result),flush=True)

with tempfile.TemporaryDirectory(prefix='s7gx-verify-') as tmp:
    root=Path(tmp); ctl=root/'ctl'; work=root/'remote'; work.mkdir()
    start=subprocess.run(['ssh','-M','-S',str(ctl),'-o','ControlPersist=no','-o','BatchMode=yes','-o','ConnectTimeout=5','-fNT','localhost'],capture_output=True,text=True,timeout=10)
    assert start.returncode==0,start.stderr
    case=BridgeTests(); case.setUp(); case.active(); proc=None
    try:
        session={'host':'localhost','control_path':str(ctl),'cwd':str(work),'reachable':True}
        case.agent.executor.set_remote_session(session)
        proc=subprocess.Popen([case.bridge.descriptor['command'],*case.bridge.descriptor['args']],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        ident=0
        def rpc(method,params=None):
            global ident
            ident+=1
            proc.stdin.write(json.dumps({'jsonrpc':'2.0','id':ident,'method':method,'params':params or {}})+'\n');proc.stdin.flush()
            assert select.select([proc.stdout],[],[],35)[0], 'MCP timeout'
            response=json.loads(proc.stdout.readline()); assert response['id']==ident,response
            return response['result']
        record('initialize',rpc('initialize',{'protocolVersion':'2025-03-26'}))
        specs=rpc('tools/list');record('discovery',[s['name'] for s in specs['tools']])
        turn=P._Turn(case.provider,case.agent,None,case.events.append,threading.Event())
        def call(adapter,name,**args):
            source=({'server':'relay_board','tool':name,'arguments':args,'_guest_tool':'mcpToolCall'} if adapter=='codex' else {**args,'_guest_tool':'mcp__relay_board__'+name})
            mapped=turn._tool_name({'tool':'other','input':source})[0]; assert mapped==name
            result=rpc('tools/call',{'name':mapped,'arguments':args})
            body=json.loads(result['content'][0]['text']);record(adapter+':'+name,body);return body
        for adapter in ['codex','claude']:
            r=call(adapter,'run_command',host='localhost',command="pwd; printf 'output-check\\n'; exit 7")
            assert r['exit_code']==7 and r['output']==str(work)+'\noutput-check\n',r
            r=call(adapter,'write_file',host='localhost',path=adapter+'.txt',content='alpha\n');assert not r.get('error'),r
            r=call(adapter,'read_file',host='localhost',path=adapter+'.txt');assert r['content']=='alpha\n',r
            r=call(adapter,'edit_file',host='localhost',path=adapter+'.txt',old_string='alpha',new_string='beta');assert not r.get('error'),r
            r=call(adapter,'read_file',host='localhost',path=adapter+'.txt');assert r['content']=='beta\n',r
            r=call(adapter,'list_directory',host='localhost',path='.');assert adapter+'.txt' in json.dumps(r),r
            r=call(adapter,'run_command',host='localhost',command='rm -- '+adapter+'.txt');assert r['exit_code']==0
            assert not (work/(adapter+'.txt')).exists()
        r=call('codex','run_command',host='localhost',command="truncate -s 8192 big.dat; truncate -s 37 small.dat; find . -type f -printf '%s %f\\n' | sort -nr")
        assert r['output']=='8192 big.dat\n37 small.dat\n',r
        for args,fragment in [({'command':'touch MUST_NOT_EXIST'},'explicit active SSH host'),({'command':'touch MUST_NOT_EXIST','host':'elsewhere'},'not the host')]:
            assert fragment in call('codex','run_command',**args)['error']
        for name,args in [('read_file',{'path':'.ssh/id_ed25519'}),('write_file',{'path':'.env','content':'secret'})]:
            assert call('claude',name,host='localhost',**args).get('error')
        case.agent.executor.set_remote_session(None)
        assert 'not logged into any host' in call('codex','run_command',host='localhost',command='touch MUST_NOT_EXIST')['error']
        case.agent.executor.set_remote_session(session)
        case.bridge.end()
        assert call('claude','run_command',host='localhost',command='touch MUST_NOT_EXIST')['code']=='unavailable'
        case.active()
        assert rpc('tools/list')==specs
        r=call('codex','run_command',host='localhost',command='sleep 20',timeout_seconds=1);assert r['still_running']; job=r['job_id']
        r=call('codex','stop_command',job_id=job);assert not r.get('error'),r
        r=call('codex','command_output',job_id=job,wait_seconds=1);assert not r.get('still_running'),r
        subprocess.run(['ssh','-S',str(ctl),'-O','exit','localhost'],check=True,capture_output=True)
        for _ in range(50):
            if not ctl.exists():break
            time.sleep(.02)
        assert 'has closed' in call('codex','run_command',host='localhost',command='touch MUST_NOT_EXIST')['error']
        # An abandoned socket still passes stat: prove OpenSSH cannot authenticate afresh.
        dead=socket.socket(socket.AF_UNIX);dead.bind(str(ctl));dead.close()
        r=call('claude','run_command',host='localhost',command='touch MUST_NOT_EXIST');assert r['exit_code']==255,r
        assert not (work/'MUST_NOT_EXIST').exists()
        r=call('claude','read_file',host='localhost',path='big.dat');assert r.get('error'),r
        record('passed',True)
    finally:
        if proc:
            proc.stdin.close();proc.wait(timeout=10)
        case.agent.executor.shutdown();case.doCleanups()
        subprocess.run(['ssh','-S',str(ctl),'-O','exit','localhost'],capture_output=True)
        paths=['backend/relay_core/'+x for x in ['guest_board_bridge.py','guest_instructions.py','remote_session.py']]+['tests/test_guest_board_bridge.py','tests/test_ssh_remote.py']
        record('source_sha256',{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
        (OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
