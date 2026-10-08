import subprocess
import sys
import threading
from http.server import ThreadingHTTPServer
from mock_server import Handler
server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.messages, server.log, server.log_path = [], [], None
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    result = subprocess.run([sys.argv[1], f'http://127.0.0.1:{server.server_port}'], timeout=40)
finally:
    server.shutdown()
sys.exit(result.returncode)
