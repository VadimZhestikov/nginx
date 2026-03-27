#!/usr/bin/env -S qjs --std
'use strict';
/*
 * repl-client.js — interactive REPL client for the nginx JS REPL.
 *
 * Usage:
 *   qjs repl-client.js [HOST] [PORT] [PATH] [WORKER]
 *
 * Defaults:
 *   HOST   = 127.0.0.1
 *   PORT   = 8080
 *   PATH   = /repl
 *   WORKER = 0
 *
 * Wire protocol (line-oriented text):
 *
 *   Client → server:
 *     "<token> EVAL <js-line>\n"
 *     "<token> COMPLETE <prefix>\n"    (tab completion)
 *     "<token> TARGET <worker_id>\n"   (switch target worker)
 *
 *   Server → client:
 *     "<token> OK [<json-value>]\n"
 *     "<token> INCOMPLETE\n"
 *     "<token> ERR <message>\n"
 *     "<token> STACK <line>\n"
 *     "<token> STACK_END\n"
 *     "* LOG console:<level> <msg>\n"
 *     "* LOG nginx:<level> <msg>\n"
 */

/* std and os are provided as globals by the --std flag */

/* ------------------------------------------------------------------ */
/* Argument parsing                                                     */
/* ------------------------------------------------------------------ */

const args   = scriptArgs.slice(1);
const host   = args[0] || '127.0.0.1';
const port   = parseInt(args[1]) || 8080;
const path   = args[2] || '/repl';
const worker = args[3] || '0';

/* ------------------------------------------------------------------ */
/* TCP helpers (synchronous, using os.read/write on the raw socket)    */
/* ------------------------------------------------------------------ */

function tcp_connect(host, port) {
    /* QuickJS doesn't have a high-level connect; use os.socket */
    const fd = os.socket(os.AF_INET, os.SOCK_STREAM, 0);
    if (fd < 0) {
        throw new Error('socket() failed: ' + (-fd));
    }

    const rc = os.connect(fd, { family: os.AF_INET, addr: host, port });
    if (rc < 0) {
        os.close(fd);
        throw new Error('connect() failed: ' + (-rc));
    }

    return fd;
}

function sock_write(fd, str) {
    const buf = std.stringToArrayBuffer(str);
    let pos = 0;
    const u8  = new Uint8Array(buf);
    while (pos < u8.length) {
        const n = os.write(fd, u8.buffer, pos, u8.length - pos);
        if (n <= 0) { throw new Error('write error'); }
        pos += n;
    }
}

/* Read one '\n'-terminated line from fd.  Returns the line without '\n'. */
function sock_read_line(fd) {
    const bytes = [];
    const tmp   = new ArrayBuffer(1);
    for (;;) {
        const n = os.read(fd, tmp, 0, 1);
        if (n <= 0) { return null; }
        const ch = new Uint8Array(tmp)[0];
        if (ch === 0x0a) { break; }      /* '\n' */
        if (ch !== 0x0d) { bytes.push(ch); }  /* skip '\r' */
    }
    return String.fromCharCode(...bytes);
}

/* ------------------------------------------------------------------ */
/* HTTP upgrade handshake                                               */
/* ------------------------------------------------------------------ */

function http_upgrade(fd, host, port, path, worker) {
    const req = [
        'GET ' + path + '?w=' + worker + '&loglevel=2&nginxlevel=4 HTTP/1.1',
        'Host: ' + host + ':' + port,
        'Connection: upgrade',
        'Upgrade: nginx-repl',
        '',
        '',
    ].join('\r\n');

    sock_write(fd, req);

    /* Read response headers */
    let status_line = '';
    let first = true;
    for (;;) {
        const line = sock_read_line(fd);
        if (line === null) { throw new Error('server closed connection'); }
        if (first) {
            status_line = line;
            first = false;
        }
        if (line === '') { break; }  /* end of headers */
    }

    /* Accept 101 or 200 */
    if (!status_line.startsWith('HTTP/1.1 10') &&
        !status_line.startsWith('HTTP/1.1 20'))
    {
        throw new Error('unexpected HTTP response: ' + status_line);
    }
}

/* ------------------------------------------------------------------ */
/* Token generator                                                      */
/* ------------------------------------------------------------------ */

let _tok = 0;
function next_token() {
    return 't' + (++_tok);
}

/* ------------------------------------------------------------------ */
/* Response reader (blocking, reads until the token is answered)        */
/* ------------------------------------------------------------------ */

/*
 * Read server lines until we see <token> OK/INCOMPLETE/ERR/STACK_END.
 * Print LOG lines immediately.
 * Returns { status, value, message, stack[] }.
 */
function read_response(fd, token) {
    const stack = [];
    for (;;) {
        const line = sock_read_line(fd);
        if (line === null) {
            std.err.puts('\n[connection closed]\n');
            os.exit(1);
        }

        /* LOG lines — print immediately */
        if (line.startsWith('* LOG ')) {
            /* "* LOG console:warn some message" */
            const rest  = line.slice(6);
            const colon = rest.indexOf(':');
            const space = rest.indexOf(' ', colon + 1);
            const src   = colon >= 0 ? rest.slice(0, colon) : 'log';
            const level = colon >= 0 && space > colon
                          ? rest.slice(colon + 1, space) : '';
            const msg   = space >= 0 ? rest.slice(space + 1) : rest;

            const prefix = src === 'nginx'
                           ? '\x1b[36m[nginx:' + level + ']\x1b[0m '
                           : '\x1b[33m[' + level + ']\x1b[0m ';
            std.err.puts(prefix + msg + '\n');
            continue;
        }

        /* Responses addressed to our token */
        if (!line.startsWith(token + ' ')) {
            continue;  /* stale line from previous request — ignore */
        }

        const rest = line.slice(token.length + 1);

        if (rest.startsWith('OK')) {
            const val = rest.length > 3 ? rest.slice(3) : undefined;
            return { status: 'ok', value: val };
        }

        if (rest === 'INCOMPLETE') {
            return { status: 'incomplete' };
        }

        if (rest.startsWith('ERR ')) {
            return { status: 'error', message: rest.slice(4), stack };
        }

        if (rest.startsWith('STACK ')) {
            stack.push(rest.slice(6));
            continue;
        }

        if (rest === 'STACK_END') {
            /* We'll get an ERR line before STACK_END in current protocol,
             * but handle it here for robustness. */
            continue;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main REPL loop                                                       */
/* ------------------------------------------------------------------ */

std.err.puts('\x1b[1mnginx JS REPL\x1b[0m  (worker ' + worker + ')\n');
std.err.puts('Type JS to evaluate.  Ctrl-D to exit.\n\n');

const fd = tcp_connect(host, port);
http_upgrade(fd, host, port, path, worker);

let pending = '';   /* accumulated incomplete multiline input */

for (;;) {
    const prompt = pending.length > 0 ? '... ' : '>>> ';
    std.out.puts(prompt);
    std.out.flush();

    const line = std.in.getline();
    if (line === null) {
        /* EOF / Ctrl-D */
        std.err.puts('\n');
        break;
    }

    const input = pending.length > 0 ? pending + '\n' + line : line;

    const tok = next_token();
    sock_write(fd, tok + ' EVAL ' + input + '\n');
    const resp = read_response(fd, tok);

    if (resp.status === 'incomplete') {
        pending = input;
        continue;
    }

    pending = '';

    if (resp.status === 'ok') {
        if (resp.value !== undefined && resp.value !== '') {
            std.out.puts('\x1b[32m' + resp.value + '\x1b[0m\n');
        }
    } else {
        std.err.puts('\x1b[31mError: ' + resp.message + '\x1b[0m\n');
        if (resp.stack && resp.stack.length > 0) {
            for (const s of resp.stack) {
                std.err.puts('  ' + s + '\n');
            }
        }
    }
}

os.close(fd);
