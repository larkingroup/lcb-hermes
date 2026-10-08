import os
import subprocess
import sys
import tempfile
import threading
from http.server import ThreadingHTTPServer
from mock_server import Handler
server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.messages, server.log, server.log_path = [], [], None
threading.Thread(target=server.serve_forever, daemon=True).start()
with tempfile.TemporaryDirectory(prefix='hermes-ui-') as folder:
    with tempfile.TemporaryFile() as ready:
        xvfb = subprocess.Popen([sys.argv[2], '-displayfd', str(ready.fileno()), '-screen', '0', '1280x900x24', '-nolisten', 'tcp'], pass_fds=[ready.fileno()], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            import time
            for _ in range(100):
                ready.seek(0)
                display = ready.read().decode().strip()
                if display:
                    break
                if xvfb.poll() is not None:
                    raise RuntimeError('Xvfb exited')
                time.sleep(.05)
            else:
                raise RuntimeError('Xvfb failed to start')
            env = dict(os.environ, DISPLAY=':'+display, XDG_DATA_HOME=folder, LC_ALL='C.UTF-8')
            result = subprocess.run([sys.argv[1], f'http://127.0.0.1:{server.server_port}'], env=env, timeout=35)
            assert not os.listdir(folder), 'UI tests must not write user settings'
            replies = [row for row in server.log if row.get('id') == 'clarify-fixture']
            assert replies and replies[-1]['result']['answers'] == {'one': 'alpha', 'two': 'beta'}
        finally:
            xvfb.terminate()
            xvfb.wait(timeout=5)
            server.shutdown()
sys.exit(result.returncode)
