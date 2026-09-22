import sys, threading, socketserver
sys.path[:0] = ['tests', 'backend']
from test_relay_profile import ConverterTests
servers=[];threads=[]
for n in range(8):
    s=socketserver.TCPServer(('127.0.0.1',0),socketserver.BaseRequestHandler)
    t=threading.Thread(target=s.serve_forever,kwargs={'poll_interval':.005});t.start()
    servers.append(s);threads.append(t)
try:
    for i in range(100):
        case=ConverterTests('test_pstats_round_trips_through_speedscope')
        try: case.test_pstats_round_trips_through_speedscope()
        except AssertionError as e:
            print('REPRODUCED attempt', i, sys.version, '\n',e)
            raise SystemExit(1)
    else: print('NO FAILURE')
finally:
    for s,t in zip(servers,threads): s.shutdown();t.join();s.server_close()
