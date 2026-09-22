"""Isolated implementer endpoint: python3 capture-server.py <capture.jsonl>."""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'{"data":[{"id":"discovered"}]}')

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        # Only a local test prompt and dummy endpoint are used; no credentials.
        with Path(sys.argv[1]).open('a') as out:
            out.write(json.dumps({'path': self.path, 'authorization': self.headers.get('Authorization'), 'body': body}) + '\n')
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.end_headers()
        self.wfile.write(b'data: {"choices":[{"delta":{"content":"Local JSON capture succeeded."},"finish_reason":"stop"}]}\n\ndata: [DONE]\n\n')

ThreadingHTTPServer(('127.0.0.1', 41868), Handler).serve_forever()
