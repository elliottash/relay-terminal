"""Real shell / real GUI recall regression, isolated from the user's settings."""
import os
from pathlib import Path
import subprocess as sp
import tempfile
import time
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent

def wait(check, message):
    end=time.monotonic()+25
    while time.monotonic()<end:
        if check(): return
        time.sleep(.15)
    raise AssertionError(message)

with tempfile.TemporaryDirectory(prefix='relay-recall-') as tmp:
    home=Path(tmp)
    config=home/'config/RelayTerminal'
    config.mkdir(parents=True)
    (home/'run').mkdir(mode=0o700)
    (config/'relay.conf').write_text('[instructions]\nonboarded=true\n[isolation]\nenabled=false\n[suggestions]\nnext_command=false\nnext_prompt=false\n[security]\napprovals_chosen=true\n[url_handler]\nannounced=true\n')
    display=next(':'+str(n) for n in range(510,550) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
    env={**os.environ,'DISPLAY':display,'QT_QPA_PLATFORM':'xcb','XDG_CONFIG_HOME':str(home/'config'),'XDG_DATA_HOME':str(home/'data'),'XDG_CACHE_HOME':str(home/'cache'),'XDG_RUNTIME_DIR':str(home/'run'),'RELAY_KEYRING':'off'}
    def x(*a): return sp.check_output(['xdotool',*a],env=env,text=True).strip()
    def key(k): x('key','--clearmodifiers',k); time.sleep(.2)
    def type_text(t): x('type','--clearmodifiers','--delay','5','--',t)
    def text():
        key('ctrl+a'); key('ctrl+c')
        value=sp.check_output(['xclip','-selection','clipboard','-o'],env=env,text=True)
        key('Right')
        return value
    xvfb=sp.Popen(['Xvfb',display,'-screen','0','1280x900x24'],stdout=sp.DEVNULL,stderr=sp.DEVNULL)
    relay=None
    try:
        time.sleep(.4)
        with (OUT/'stderr.log').open('w') as err:
            relay=sp.Popen([os.environ.get('RELAY_TEST_BINARY',str(ROOT/'build/relay')),'--workspace',tmp],env=env,stdout=err,stderr=err)
            time.sleep(3)
            wins=x('search','--pid',str(relay.pid)).splitlines()
            win=max(wins,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
            x('windowfocus','--sync',win)
            first='printf recalled > '+str(home/'recalled')
            second='printf remaining > '+str(home/'remaining')
            type_text('!touch '+str(home/'started')+'; sleep 12'); key('Return')
            wait(lambda:(home/'started').exists(),'shell did not start')
            type_text('!'+first); key('Return')
            type_text('!'+second); key('Return'); key('Up')
            assert text()==first, 'Up did not recall first queued command'
            sp.run(['import','-window',win,str(OUT/'recalled.png')],env=env,check=True)
            wait(lambda:(home/'remaining').exists(),'remaining queue stayed paused')
            assert not (home/'recalled').exists(), 'recalled command ran without submission'
            assert text()==first, 'queue completion overwrote the draft'
            key('Return')
            wait(lambda:(home/'recalled').exists(),'resubmitted draft did not run')
            type_text('!sleep 5'); key('Return')
            third='printf single > '+str(home/'single')
            type_text('!'+third); key('Return'); key('Up')
            assert text()==third
            time.sleep(6)
            assert not (home/'single').exists(), 'single recalled command ran'
            type_text(' # edited'); key('Return')
            wait(lambda:(home/'single').exists(),'edited draft did not run')
            sp.run(['import','-window',win,str(OUT/'resubmitted.png')],env=env,check=True)
            print('PASS: recalled first item; remaining queue runs; draft survives; Enter resubmits; single-item edit/resubmit passes.')
    finally:
        if relay:
            relay.terminate()
            try: relay.wait(timeout=5)
            except sp.TimeoutExpired: relay.kill(); relay.wait()
        xvfb.terminate(); xvfb.wait()
