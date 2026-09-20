import json,sys
j=json.load(open(sys.argv[1]))
print('tag',j['tag'],'built',j.get('panes_built'),'panes',j['panes'],'tabs',j['tabs'],'load',j['load'],'| total_cpu%',j['total_cpu_pct'],'total_wakeups/s',j['total_wakeups_s'],'nproc',j['nproc'])
for r in j['rows']: print(f"  {r['cmd'][:26]:28s} pid={r['pid']} thr={r['threads']:3d} cpu%={r['cpu_pct']:6.2f} wk/s={r['wakeups_s']:7.1f} pss={r['pss_kb']} pdirty={r['pdirty_kb']}")
print('  SUM Pss kB', sum(r['pss_kb'] or 0 for r in j['rows']), ' SUM Private_Dirty kB', sum(r['pdirty_kb'] or 0 for r in j['rows']))
