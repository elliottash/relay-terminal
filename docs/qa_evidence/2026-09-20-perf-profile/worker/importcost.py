import subprocess, sys, os, json, time
mods = sys.argv[1:]
env = dict(os.environ); env["PYTHONPATH"]="."
def cost(code):
    best=1e9
    for _ in range(5):
        t=time.perf_counter()
        subprocess.run([sys.executable,"-S","-c",code],env=env,check=True)
        best=min(best,time.perf_counter()-t)
    return best
base=cost("pass")
print(f"interpreter baseline: {base*1000:.1f} ms")
for m in mods:
    print(f"{m}: {(cost('import '+m)-base)*1000:.1f} ms")
