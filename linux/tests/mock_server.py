#!/usr/bin/env python3
"""Isolated Hermes contract fixture; never contacts a real server."""
import argparse
import base64
import hashlib
import json
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, fmt, *args):
        pass
    def json(self, value, status=200, cookie=False):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        if cookie:
            self.send_header('Set-Cookie', 'hermes_session=fixture; HttpOnly; Path=/')
        self.end_headers()
        self.wfile.write(body)
    def authenticated(self):
        return 'hermes_session=fixture' in self.headers.get('Cookie', '')
    def do_GET(self):
        if self.path == '/api/status':
            self.json({'version': 'test-fixture'})
        elif self.path.startswith('/api/ws?ticket=fixture'):
            if not self.authenticated():
                self.json({'error': 'cookie missing'}, 401)
                return
            self.websocket()
        elif self.path == '/api/config' and self.authenticated():
            self.json({'terminal': {'cwd': '/fixture'}})
        elif self.path == '/api/system/stats' and self.authenticated():
            self.json({'hermes_version': 'fixture', 'hostname': 'Local test server', 'cpu_percent': 3.5,
                       'memory': {'used': 1073741824, 'total': 8589934592}, 'process': {'rss': 73400320}})
        elif self.path == '/api/cron/jobs' and self.authenticated():
            self.json([])
        else:
            self.json({}, 404)
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        data = json.loads(body or b'{}')
        if self.path == '/auth/password-login':
            if data.get('username') == 'fixture' and data.get('password') == 'fixture':
                self.json({'ok': True}, cookie=True)
            else:
                self.json({'error': 'wrong login'}, 401)
        elif self.path == '/api/auth/ws-ticket' and self.authenticated():
            self.json({'ticket': 'fixture'})
        else:
            self.json({}, 401)
    def websocket(self):
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        self.send_header('Upgrade', 'websocket')
        self.send_header('Connection', 'Upgrade')
        self.send_header('Sec-WebSocket-Accept', accept)
        self.end_headers()
        seq = 0
        def send(value, opcode=1):
            body = json.dumps(value, ensure_ascii=False).encode() if opcode == 1 else value
            head = bytes([0x80 | opcode])
            if len(body) < 126:
                head += bytes([len(body)])
            elif len(body) < 65536:
                head += bytes([126]) + struct.pack('!H', len(body))
            else:
                head += bytes([127]) + struct.pack('!Q', len(body))
            self.wfile.write(head + body)
            self.wfile.flush()
        def event(kind, payload=None):
            nonlocal seq
            seq += 1
            send({'jsonrpc': '2.0', 'method': 'event', 'params': {'type': kind, 'session_id': 'runtime', 'seq': seq, 'payload': payload or {}}})
        def loaded():
            return {'session_id': 'runtime', 'session_key': 'saved',
                    'info': {'title': 'Local contract test', 'cwd': '/fixture', 'model': 'fixture-model', 'provider': 'fixture'},
                    'messages': self.server.messages, 'open_requests': []}
        send({'jsonrpc': '2.0', 'method': 'event', 'params': {'type': 'gateway.ready', 'payload': {'replay_epoch': 'fixture'}}})
        try:
            while True:
                head = self.rfile.read(2)
                if len(head) != 2:
                    break
                opcode = head[0] & 15
                size = head[1] & 127
                if size == 126:
                    size = struct.unpack('!H', self.rfile.read(2))[0]
                elif size == 127:
                    size = struct.unpack('!Q', self.rfile.read(8))[0]
                if size > 8 * 1024 * 1024:
                    break
                mask = self.rfile.read(4) if head[1] & 128 else b''
                body = self.rfile.read(size)
                if mask:
                    body = bytes(v ^ mask[i % 4] for i, v in enumerate(body))
                if opcode == 8:
                    send(body, 8)
                    break
                if opcode == 9:
                    send(body, 10)
                    continue
                if opcode != 1:
                    continue
                request = json.loads(body)
                method = request.get('method')
                if not method:
                    self.server.log.append(request)
                    continue
                params = request.get('params', {})
                result = {}
                self.server.log.append({'method': method, 'params': params})
                if method == 'session.list':
                    result = {'sessions': [{'id': 'saved', 'title': 'Local contract test', 'started_at': 1791460000}]}
                elif method in ('session.resume', 'session.create'):
                    result = loaded()
                elif method == 'projects.tree':
                    result = {'projects': [{'isNoProject': True, 'previewSessions': [{'id': 'saved', 'cwd': None}]}]}
                elif method == 'projects.list':
                    result = {'projects': []}
                elif method == 'config.get':
                    result = {'value': 'fixture-model'}
                elif method == 'model.options':
                    result = {'providers': [{'slug': 'fixture', 'models': ['fixture-model']}], 'provider': 'fixture'}
                elif method == 'prompt.submit':
                    self.server.messages.append({'role': 'user', 'text': params.get('text', '')})
                    result = {'accepted': True}
                send({'jsonrpc': '2.0', 'id': request['id'], 'result': result})
                if method == 'prompt.submit':
                    event('message.start')
                    event('message.delta', {'text': 'Hello from the local fixture. café / λ / 日本語'})
                    time.sleep(0.05)
                    event('message.complete', {'text': 'Hello from the local fixture. café / λ / 日本語', 'status': 'success'})
                    self.server.messages.append({'role': 'assistant', 'text': 'Hello from the local fixture. café / λ / 日本語'})
                    send({'jsonrpc': '2.0', 'id': 'approve-fixture', 'method': 'approval',
                          'params': {'session_id': 'runtime', 'description': 'Local fixture asks for permission',
                                     'command': 'echo fixture', 'choices': ['once', 'deny']}})
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            if self.server.log_path:
                with open(self.server.log_path, 'w') as file:
                    json.dump(self.server.log, file, indent=2)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=18765)
    parser.add_argument('--log')
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.messages = []
    server.log = []
    server.log_path = args.log
    print(f'Fixture at http://127.0.0.1:{server.server_port}; user/password: fixture', flush=True)
    server.serve_forever()

if __name__ == '__main__':
    main()
