import json,sys,time

def emit(event,**kw):
 print(json.dumps(dict(event=event,**kw)),flush=True)
emit('ready')
for line in sys.stdin:
 try:r=json.loads(line)
 except ValueError:continue
 t=r.get('type'); rid=r.get('id','')
 if t=='configure':emit('configured',model='fixture',context=r.get('context',{}),agent_role='switchboard')
 elif t=='board_open':
  emit('board',config={'columns':['inbox','done'],'tabs':[{'id':'features','folder':'features'}]},cards=[{'id':'K7Q2','title':'Cleanup fixture card','status':'inbox','type':'work','tab':'features','rank':'m'}],problems=[])
 elif t=='board_cleanup':
  dry=r.get('dry_run',True)
  emit('board_cleanup_started',id=rid,run_id='fixture-run',dry_run=dry,cards=1)
  emit('agent_started',id=rid)
  emit('tool_started',cleanup=True,run_id='fixture-run',turn_id=rid,call_id='c1',tool='board_list',preview='Read the board')
  time.sleep(.2)
  emit('tool_result',cleanup=True,run_id='fixture-run',turn_id=rid,call_id='c1',tool='board_list',text='Read one card')
  if not dry:emit('board_activity',cleanup=True,run_id='fixture-run',id='K7Q2',summary='moved to ready')
  emit('board_cleanup_summary',id=rid,run_id='fixture-run',dry_run=dry,outcome='done',counts={'proposed':1,'writes':1},cards_before=1,cards_after=1,changes=[{'card_id':'K7Q2','action':'move','summary':'move to ready'}],refusals=[],changelog='docs/cleanup-fixture.md')
  emit('done',cleanup=True,run_id='fixture-run')
  emit('agent_finished',id=rid,outcome='done')
 elif t=='shutdown':break
