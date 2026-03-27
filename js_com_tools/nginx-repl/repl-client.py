#!/usr/bin/env python3
"""
repl-client.py — interactive REPL client for the nginx JS REPL.

Usage:
  python3 tools/repl-client.py [HOST] [PORT] [PATH]

Defaults:
  HOST = 127.0.0.1
  PORT = 8099
  PATH = /repl/

Wire protocol (line-oriented text):
  Client → server:  "<token> EVAL <js>\n"
  Server → client:  "<token> OK [value]\n"
                    "<token> INCOMPLETE\n"
                    "<token> ERR <message>\n"
                    "<token> STACK <line>\n"
                    "<token> STACK_END\n"
                    "* LOG console:<level> <msg>\n"
                    "* LOG nginx:<level> <msg>\n"
"""

import sys
import socket
import select
import threading

# ── argument parsing ──────────────────────────────────────────────────────────
args   = sys.argv[1:]
host   = args[0] if len(args) > 0 else '127.0.0.1'
port   = int(args[1]) if len(args) > 1 else 8099
path   = args[2] if len(args) > 2 else '/repl/'
worker = int(args[3]) if len(args) > 3 else -1   # -1 = any worker

# ── readline (optional — graceful degradation if unavailable) ─────────────────
try:
    import readline  # noqa: F401  enables arrow keys / history
except ImportError:
    pass

# ── ANSI helpers ──────────────────────────────────────────────────────────────
def green(s):  return f'\x1b[32m{s}\x1b[0m'
def red(s):    return f'\x1b[31m{s}\x1b[0m'
def cyan(s):   return f'\x1b[36m{s}\x1b[0m'
def yellow(s): return f'\x1b[33m{s}\x1b[0m'
def bold(s):   return f'\x1b[1m{s}\x1b[0m'

# ── TCP connect + HTTP upgrade ────────────────────────────────────────────────
def _connect_and_upgrade():
    """Connect to the REPL endpoint.  Returns (sock, leftover_bytes).
    When WORKER >= 0 the server relays all evals to that worker via a
    SharedWorker broker — no retry needed."""
    qs = 'loglevel=2&nginxlevel=4'
    if worker >= 0:
        qs += f'&w={worker}'

    s = socket.create_connection((host, port), timeout=10)
    s.settimeout(None)

    req = (
        f'GET {path}?{qs} HTTP/1.1\r\n'
        f'Host: {host}:{port}\r\n'
        f'Connection: upgrade\r\n'
        f'Upgrade: nginx-repl\r\n'
        f'\r\n'
    )
    s.sendall(req.encode())

    buf = b''
    while b'\r\n\r\n' not in buf:
        chunk = s.recv(4096)
        if not chunk:
            s.close()
            sys.exit('server closed connection during handshake')
        buf += chunk

    headers_raw, _, leftover = buf.partition(b'\r\n\r\n')
    status_line = headers_raw.split(b'\r\n')[0].decode(errors='replace')

    if '101' in status_line or '200' in status_line:
        return s, leftover

    s.close()
    sys.exit(f'upgrade failed: {status_line}')


sock, leftover = _connect_and_upgrade()

# ── shared line buffer (background reader thread → main thread) ───────────────
_line_buf  = bytearray(leftover)   # any bytes already received after headers
_line_lock = threading.Lock()
_lines     = []          # complete lines ready for the main thread
_log_lines = []          # LOG lines buffered while waiting for a token reply
_closed    = False

def _reader():
    """Background thread: reads bytes from the socket into complete lines."""
    global _closed
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            with _line_lock:
                _line_buf.extend(data)
                while b'\n' in _line_buf:
                    idx = _line_buf.index(b'\n')
                    line = _line_buf[:idx].rstrip(b'\r').decode(errors='replace')
                    del _line_buf[:idx + 1]
                    _lines.append(line)
    except Exception:
        pass
    _closed = True

threading.Thread(target=_reader, daemon=True).start()

# ── token counter ─────────────────────────────────────────────────────────────
_tok = 0
def next_token():
    global _tok
    _tok += 1
    return f't{_tok}'

# ── send one command, collect the reply ───────────────────────────────────────
def send_eval(js_line):
    tok = next_token()
    sock.sendall(f'{tok} EVAL {js_line}\n'.encode())

    stack = []
    while True:
        # Spin-wait for a line (the reader thread feeds _lines)
        while True:
            with _line_lock:
                if _lines:
                    line = _lines.pop(0)
                    break
            if _closed:
                return {'status': 'error', 'message': '[connection closed]', 'stack': []}
            select.select([], [], [], 0.05)

        if line.startswith('* LOG '):
            _print_log(line)
            continue

        if not line.startswith(tok + ' '):
            continue  # stale line from a previous request

        rest = line[len(tok) + 1:]

        if rest.startswith('OK'):
            val = rest[3:] if len(rest) > 3 else None
            return {'status': 'ok', 'value': val}

        if rest == 'INCOMPLETE':
            return {'status': 'incomplete'}

        if rest.startswith('ERR '):
            return {'status': 'error', 'message': rest[4:], 'stack': stack}

        if rest.startswith('STACK '):
            stack.append(rest[6:])
            continue

        if rest == 'STACK_END':
            continue

# ── log-line pretty printer ───────────────────────────────────────────────────
def _print_log(line):
    """Print a '* LOG …' line to stderr with colour."""
    rest  = line[6:]           # strip '* LOG '
    colon = rest.find(':')
    space = rest.find(' ', colon + 1) if colon >= 0 else -1
    src   = rest[:colon] if colon >= 0 else 'log'
    level = rest[colon + 1:space] if (colon >= 0 and space > colon) else ''
    msg   = rest[space + 1:] if space >= 0 else rest

    if src == 'nginx':
        prefix = cyan(f'[nginx:{level}]')
    else:
        prefix = yellow(f'[{level}]')

    # Re-print prompt after log line so the cursor ends up in the right place
    print(f'\r{prefix} {msg}', file=sys.stderr)

# ── main REPL loop ────────────────────────────────────────────────────────────
worker_label = f'  → worker {worker}' if worker >= 0 else ''
print(bold('nginx JS REPL') + f'  {host}:{port}{path}' + worker_label, file=sys.stderr)
print('Type JS to evaluate.  Ctrl-D to exit.\n', file=sys.stderr)

pending = ''   # accumulated incomplete multiline input

while True:
    prompt = '... ' if pending else '>>> '
    try:
        line = input(prompt)
    except EOFError:
        print('', file=sys.stderr)
        break
    except KeyboardInterrupt:
        print('', file=sys.stderr)
        pending = ''
        continue

    js = (pending + '\n' + line) if pending else line

    resp = send_eval(js)

    if resp['status'] == 'incomplete':
        pending = js
        continue

    pending = ''

    if resp['status'] == 'ok':
        if resp['value'] is not None and resp['value'] != '':
            print(green(resp['value']))
    else:
        print(red('Error: ' + resp['message']), file=sys.stderr)
        for frame in resp.get('stack', []):
            print('  ' + frame, file=sys.stderr)

sock.close()
