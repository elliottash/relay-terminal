import re,sys
ln=list(open('sp/trace.txt',errors='replace'))
def ts(l):
    m=re.search(r'(\d\d):(\d\d):(\d\d\.\d+)',l)
    return int(m.group(1))*3600+int(m.group(2))*60+float(m.group(3)) if m else None
t0=None; marks=[]
pats=[(r'execve\(".*relay"','exec relay'),(r'openat.*libQt5Core','dlopen libQt5Core'),
 (r'openat.*libQt5Widgets','dlopen libQt5Widgets'),(r'openat.*libQt5Gui','dlopen libQt5Gui'),
 (r'connect\(.*X11-unix','X server connect'),(r'openat.*fontconfig.*cache','fontconfig cache'),
 (r'openat.*fonts\.conf','fonts.conf'),(r'openat.*relay\.log','first relay.log write (gui_start)'),
 (r'execve.*worker\.py','execve worker.py'),(r'openat\(.*ptmx','open /dev/ptmx'),
 (r'execve.*systemd-run','execve systemd-run'),(r'execve.*"/bin/bash"','execve bash'),
 (r'openat.*libpython','worker dlopen libpython'),(r'openat.*qt5ct|openat.*platformthemes','platform theme'),
 (r'openat.*\.ttf|openat.*\.otf','first font file'),(r'openat.*state\.json','state.json')]
for l in ln:
    t=ts(l)
    if t is None: continue
    if t0 is None and re.search(r'execve\(.*relay',l): t0=t
    if t0 is None: continue
    d=(t-t0)*1000
    for p,n in pats:
        if re.search(p,l): marks.append((d,n))
first={}; last={}; cnt={}
for d,n in marks:
    first.setdefault(n,d); last[n]=d; cnt[n]=cnt.get(n,0)+1
for n,d in sorted(first.items(),key=lambda x:x[1]):
    print(f"{d:8.1f} ms  first {n:38s} (n={cnt[n]}, last {last[n]:.1f} ms)")
