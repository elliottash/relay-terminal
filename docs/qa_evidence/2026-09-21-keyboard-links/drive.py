"""Exercise #GWXM in an isolated Relay/Xvfb; external opens go to a recorder."""
import os, subprocess, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='gwxm-') as tmp:
    tmp=Path(tmp)
    for p in ['config/RelayTerminal','data','cache','run','work/folder','bin']:
        (tmp/p).mkdir(parents=True)
    (tmp/'run').chmod(0o700)
    (tmp/'work/sample.txt').write_text('first line\nsecond line\nthird line\n')
    (tmp/'work/show.sh').write_text("printf 'folder\\nsample.txt:2\\nhttps://example.com/gwxm\\n'\n")
    (tmp/'config/RelayTerminal/relay.conf').write_text('''[instructions]
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
    opener=tmp/'bin/xdg-open'
    opener.write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> "$GWXM_EXTERNAL"\n')
    opener.chmod(0o755)
    display=next(':'+str(n) for n in range(510,550) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
    env={**os.environ,'XDG_CONFIG_HOME':str(tmp/'config'),'XDG_DATA_HOME':str(tmp/'data'),
         'XDG_CACHE_HOME':str(tmp/'cache'),'XDG_RUNTIME_DIR':str(tmp/'run'),'DISPLAY':display,
         'QT_QPA_PLATFORM':'xcb','RELAY_KEYRING':'off','PATH':str(tmp/'bin')+':'+os.environ['PATH'],
         'GWXM_EXTERNAL':str(tmp/'external.txt'),'BROWSER':str(opener)}
    xvfb=subprocess.Popen(['Xvfb',display,'-screen','0','1500x950x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    relay=None
    def x(*args): return subprocess.check_output(['xdotool',*args],env=env,text=True).strip()
    def key(*keys): x('key','--clearmodifiers',*keys); time.sleep(.5)
    def shot(name):
        subprocess.run(['import','-window',win,str(OUT/(name+'.png'))],env=env,check=True)
        txt=subprocess.check_output(['tesseract',str(OUT/(name+'.png')),'stdout','--psm','11'],stderr=subprocess.DEVNULL,text=True)
        (OUT/(name+'.txt')).write_text(txt)
        print(name, txt[-450:],flush=True)
    try:
        time.sleep(.5)
        with (OUT/'gui-stderr.log').open('w') as log:
            relay=subprocess.Popen([os.environ.get('RELAY_TEST_BINARY',str(ROOT/'build/relay')),'--workspace',str(tmp/'work')],env=env,stdout=log,stderr=log)
            time.sleep(5)
            wins=x('search','--onlyvisible','--pid',str(relay.pid)).splitlines()
            win=max(wins,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
            x('windowmove',win,'0','0');x('windowsize',win,'1500','950');x('windowfocus',win)
            key('F12');x('type','--delay','10','bash show.sh');key('Return');time.sleep(1)
            key('F12');key('ctrl+shift+l');shot('01-url-selected')
            key('Return');time.sleep(1)
            key('ctrl+shift+l');key('Up');shot('02-file-selected')
            key('Return');shot('03-file-preview')
            key('ctrl+shift+w');key('ctrl+shift+l');key('Up');key('ctrl+Return');shot('04-file-edit')
            key('ctrl+shift+w');key('ctrl+shift+l');key('Up');key('shift+Return');time.sleep(1)
            key('ctrl+shift+l');key('Up','Up');shot('05-folder-selected')
            key('Return');shot('06-folder-explorer')
            key('ctrl+shift+w');key('ctrl+shift+l');key('Up','Up');key('shift+Return');time.sleep(1)
            key('ctrl+shift+l');key('Escape');key('shift+Tab');shot('07-plan-after-escape')
            assert 'save' in (OUT/'04-file-edit.txt').read_text().lower()
            assert 'date modified' in (OUT/'06-folder-explorer.txt').read_text().lower()
            external=(tmp/'external.txt').read_text() if (tmp/'external.txt').exists() else ''
            (OUT/'external-opens.txt').write_text(external)
            print('EXTERNAL:',external,flush=True)
            assert 'https://example.com/gwxm' in external
            assert 'sample.txt' in external
            assert 'folder' in external
    finally:
        if relay: relay.terminate();relay.wait(timeout=10)
        xvfb.terminate();xvfb.wait(timeout=5)
