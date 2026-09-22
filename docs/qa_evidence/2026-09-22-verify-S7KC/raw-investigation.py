import os, sys, tempfile, fcntl, termios, struct, select
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]; sys.path.insert(0,str(ROOT))
from tests.test_ssh_shell import PtyShell, typed_line
OUT=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
 env=dict(os.environ, ZDOTDIR=tmp, TERM='xterm-256color', PS1='RP> ', HISTFILE='/dev/null', LANG='C.UTF-8')
 for k in ['PROMPT_COMMAND','PS0','RELAY_R']:env.pop(k,None)
 for argv,name,command in [(['zsh','-f'],'zsh',b"printf 'ZSH-OUTPUT\\n'; false"),(['bash','--noprofile','--norc','-i'],'bash-wrap',b'printf "WRAP-%s\\n" '+b'x'*180)]:
  s=PtyShell(argv,env)
  try:
   s.run(typed_line(2))
   fcntl.ioctl(s.master,termios.TIOCSWINSZ,struct.pack('HHHH',38,113,0,0))
   s.run('true')
   s.output=b''
   os.write(s.master,command+b'\x18\x10\r')
   s.wait_prompts(1)
   while select.select([s.master],[],[],.2)[0]:s.output+=os.read(s.master,65536)
   (OUT/(name+'.raw')).write_bytes(s.output)
   print(name,repr(s.output))
  finally:s.close()
