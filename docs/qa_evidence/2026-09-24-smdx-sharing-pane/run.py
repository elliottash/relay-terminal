"""Compile driver.cpp against the built librelay-sharing.a and run it under Xvfb; PNGs land here."""
import os, pathlib, shlex, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[3]
out = pathlib.Path(__file__).resolve().parent
build = root / 'build'
qt = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'Qt5Widgets'], text=True))
libs = [str(build / f'librelay-{name}.a') for name in
        ('sharing', 'remotesettings', 'settings', 'agentcontext', 'outputlinks', 'settingscache')]
with tempfile.TemporaryDirectory(prefix='smdx-') as tmp:
    tmp = pathlib.Path(tmp)
    subprocess.run(['c++', '-std=c++17', '-fPIC', '-I' + str(root / 'src'), str(out / 'driver.cpp'),
                    '-o', str(tmp / 'driver'), *libs, *qt], check=True)
    env = dict(os.environ, XDG_CONFIG_HOME=str(tmp / 'config'), XDG_DATA_HOME=str(tmp / 'data'),
               RELAY_KEYRING='off')
    subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1280x1024x24', str(tmp / 'driver'), str(out)],
                   env=env, check=True)
print((out / 'driver.log').read_text())
