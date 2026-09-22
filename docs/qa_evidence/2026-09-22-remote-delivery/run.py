"""Compile the actual RemoteShare dialog and drive its lifecycle under isolated Xvfb."""
import pathlib, shlex, subprocess, tempfile, os
root = pathlib.Path(__file__).resolve().parents[3]
out = pathlib.Path(__file__).resolve().parent
build = root / 'build'
with tempfile.TemporaryDirectory(prefix='remote-delivery-') as tmp:
    tmp = pathlib.Path(tmp)
    qt = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt5Widgets','Qt5Network'],text=True))
    link = shlex.split((build/'CMakeFiles/relay.dir/link.txt').read_text())
    libs = link[link.index('librelay-closedlist.a'):]
    subprocess.run(['c++','-std=c++17','-fPIC','-I'+str(root/'src'), str(out/'driver.cpp'),
                    str(build/'relay_autogen/UVLADIE3JM/moc_RemoteShare.cpp'),
                    str(build/'CMakeFiles/relay.dir/src/RemoteShare.cpp.o'),
                    str(build/'CMakeFiles/relay.dir/src/Theme.cpp.o'),
                    '-o',str(tmp/'driver'),*libs,*qt],cwd=build,check=True)
    env = dict(os.environ, XDG_CONFIG_HOME=str(tmp/'config'), XDG_DATA_HOME=str(tmp/'data'),
               RELAY_KEYRING='off')
    subprocess.run(['xvfb-run','-a',str(tmp/'driver'),str(out)],env=env,check=True)
