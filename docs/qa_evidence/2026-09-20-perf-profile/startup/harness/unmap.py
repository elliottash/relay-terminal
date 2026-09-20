#!/usr/bin/env python3
"""Wakeups and CPU with the window mapped vs unmapped (= minimised). idle.tunePoll() only asks
isVisible(), so this says whether a minimised Relay stops polling."""
import sys,os,time,subprocess
BASE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,BASE)
import harness, idle
DISP=os.environ.get("DISPLAY",":231")
panes=int(sys.argv[1]) if len(sys.argv)>1 else 4
r=harness.run("unmap",os.path.join(BASE,"unmap"),["--fresh"],DISP); pid=r["pid"]
time.sleep(5); idle.build_layout(pid,panes,1); time.sleep(6)
def wk(secs):
    a=idle.switches(pid); c0=idle.cpu_ticks(pid); t=time.monotonic(); time.sleep(secs)
    w=time.monotonic()-t; b=idle.switches(pid); c1=idle.cpu_ticks(pid)
    return round((b[0]-a[0]+b[1]-a[1])/w,1), round((c1-c0)/os.sysconf('SC_CLK_TCK')/w*100,2)
print("panes",idle.panecount(pid),"load",open('/proc/loadavg').read().split()[0])
print("mapped  ",wk(30),flush=True)
w=idle.focus(pid); subprocess.run(f"xdotool windowunmap {w}",shell=True,env={**os.environ,"DISPLAY":DISP}); time.sleep(2)
print("unmapped",wk(30),flush=True)
subprocess.run(f"xdotool windowmap {w}",shell=True,env={**os.environ,"DISPLAY":DISP}); time.sleep(2)
print("remapped",wk(30),flush=True)
harness.PROCS[pid].terminate(); harness.PROCS[pid].wait(60)
