// mirror/transpile — a TCL/iRules -> mirror-JS transpiler (decision A).
//
// This is the migration layer of the mirror project's hybrid stance: build the
// clean iRules-INSPIRED JS model first (phases 1-8), then mechanically translate
// the existing iRules install base onto it. Transpiling real iRules is also the
// sharpest test that the mirror event/command model actually COVERS the iRules
// surface — every command that maps cleanly is one the model got right.
//
// Scope (first cut): the connection-lifecycle spine and the commands the mirror
// model already implements. Anything outside the supported vocabulary is NOT
// silently dropped — it is emitted as a commented-out line and reported in
// `warnings`, so a human sees exactly what still needs hand-porting.
//
// Usage:
//   var out = mirror.transpile(tclSource);
//   //   out.events   -> ['onClientAccept', ...]
//   //   out.handlers -> "{ onClientAccept: function (ev) { ... }, ... }"
//   //   out.warnings -> ["line 7: unsupported command 'sideband ...'", ...]
//   //   out.isStream -> true if the rule uses L4 (onClientData) events
//
// Works both inside nginx (attaches to globalThis.mirror) and under plain qjs
// (defines globalThis.mirrorTranspile) — the transpiler itself has no nginx deps.

(function () {
    'use strict';

    // iRules event  ->  mirror event
    var EVENT_MAP = {
        CLIENT_ACCEPTED:       'onClientAccept',
        CLIENT_DATA:           'onClientData',
        CLIENT_CLOSED:         'onClientClose',
        CLIENTSSL_CLIENTHELLO: 'onClientHello',
        HTTP_REQUEST:          'onRequestHeaders',
        HTTP_RESPONSE:         'onResponseHeaders'
    };
    var STREAM_EVENTS = { onClientData: true };

    function quote(s) { return JSON.stringify(String(s)); }

    // ---- TCL scanning --------------------------------------------------------
    // Split a script into top-level commands. Respects {} [] "" nesting; commands
    // are separated by newline or ';'; a '#' where a command would start is a
    // comment to end of line.
    function splitCommands(src) {
        var cmds = [], buf = '', i = 0, n = src.length, atStart = true;
        while (i < n) {
            var c = src[i];
            if (atStart && (c === ' ' || c === '\t' || c === '\n' || c === '\r' || c === ';')) {
                i++; continue;                          // eat leading separators
            }
            if (atStart && c === '#') {                 // comment line
                while (i < n && src[i] !== '\n') { i++; }
                continue;
            }
            atStart = false;
            if (c === '{' || c === '[') {
                var close = (c === '{') ? '}' : ']', depth = 0;
                while (i < n) {
                    if (src[i] === c) { depth++; }
                    else if (src[i] === close) { depth--; }
                    buf += src[i++];
                    if (depth === 0) { break; }
                }
                continue;
            }
            if (c === '"') {
                buf += src[i++];
                while (i < n && src[i] !== '"') {
                    if (src[i] === '\\') { buf += src[i++]; }
                    if (i < n) { buf += src[i++]; }
                }
                if (i < n) { buf += src[i++]; }
                continue;
            }
            if (c === '\n' || c === ';') {              // command terminator
                if (buf.trim().length) { cmds.push(buf.trim()); }
                buf = ''; atStart = true; i++;
                continue;
            }
            buf += src[i++];
        }
        if (buf.trim().length) { cmds.push(buf.trim()); }
        return cmds;
    }

    // Split one command into words. Each word: { type, text }.
    // type: 'brace' {..}, 'bracket' [..], 'dquote' "..", 'bare'.
    function splitWords(cmd) {
        var words = [], i = 0, n = cmd.length;
        while (i < n) {
            while (i < n && (cmd[i] === ' ' || cmd[i] === '\t' ||
                             cmd[i] === '\n' || cmd[i] === '\r')) { i++; }
            if (i >= n) { break; }
            var c = cmd[i];
            if (c === '{' || c === '[') {
                var close = (c === '{') ? '}' : ']', depth = 0, s = '';
                while (i < n) {
                    if (cmd[i] === c) { depth++; }
                    else if (cmd[i] === close) { depth--; }
                    s += cmd[i++];
                    if (depth === 0) { break; }
                }
                words.push({ type: (c === '{') ? 'brace' : 'bracket',
                             text: s.slice(1, -1) });
                continue;
            }
            if (c === '"') {
                var q = ''; i++;
                while (i < n && cmd[i] !== '"') {
                    if (cmd[i] === '\\') { q += cmd[i++]; }
                    if (i < n) { q += cmd[i++]; }
                }
                i++;
                words.push({ type: 'dquote', text: q });
                continue;
            }
            var b = '';
            while (i < n && cmd[i] !== ' ' && cmd[i] !== '\t' &&
                   cmd[i] !== '\n' && cmd[i] !== '\r') { b += cmd[i++]; }
            words.push({ type: 'bare', text: b });
        }
        return words;
    }

    // ---- value translation ---------------------------------------------------
    // A connection-scoped iRules variable maps to mirror flow-local.
    function varRef(name) { return 'ev.flow.' + name.replace(/[^A-Za-z0-9_$]/g, '_'); }

    // Interpolate a "..." / bare word: literal text with $var and [cmd] subs.
    function interp(text, warnings, lineNo) {
        var parts = [], lit = '', i = 0, n = text.length;
        function flush() { if (lit.length) { parts.push(quote(lit)); lit = ''; } }
        while (i < n) {
            var c = text[i];
            if (c === '$') {
                flush(); i++;
                var name = '';
                while (i < n && /[A-Za-z0-9_:]/.test(text[i])) { name += text[i++]; }
                parts.push(varRef(name));
            } else if (c === '[') {
                flush();
                var depth = 0, s = '';
                while (i < n) {
                    if (text[i] === '[') { depth++; }
                    else if (text[i] === ']') { depth--; }
                    s += text[i++];
                    if (depth === 0) { break; }
                }
                parts.push(commandSub(s.slice(1, -1), warnings, lineNo));
            } else {
                lit += c; i++;
            }
        }
        flush();
        if (parts.length === 0) { return quote(''); }
        return parts.length === 1 ? parts[0] : parts.join(' + ');
    }

    // Translate a word used as a VALUE into a JS expression.
    function value(word, warnings, lineNo) {
        if (word.type === 'bracket') { return commandSub(word.text, warnings, lineNo); }
        if (word.type === 'brace')   { return quote(word.text); }        // no subst
        if (word.type === 'dquote')  { return interp(word.text, warnings, lineNo); }
        // bare
        var t = word.text;
        if (/^-?\d+$/.test(t))       { return t; }                       // integer
        if (/^-?\d*\.\d+$/.test(t))  { return t; }                       // float
        if (t.indexOf('$') >= 0 || t.indexOf('[') >= 0) {
            return interp(t, warnings, lineNo);
        }
        return quote(t);
    }

    // Translate a [command substitution] into a JS expression.
    function commandSub(inner, warnings, lineNo) {
        var w = splitWords(inner);
        if (!w.length) { return 'undefined'; }
        var cmd = w[0].text;
        switch (cmd) {
        case 'IP::client_addr':
        case 'IP::remote_addr':
            return 'ev.clientAddr';
        case 'IP::remote_port':
        case 'TCP::client_port':
        case 'TCP::remote_port':
            return 'ev.clientPort';
        case 'HTTP::uri':    return 'ev.uri';
        case 'HTTP::method': return 'ev.method';
        case 'HTTP::header':
            if (w.length >= 2) { return 'ev.header(' + value(w[1], warnings, lineNo) + ')'; }
            break;
        case 'table':
            if (w.length >= 3 && (w[1].text === 'lookup' || w[1].text === 'get')) {
                return 'ev.table.get(' + value(w[2], warnings, lineNo) + ')';
            }
            break;
        case 'expr':
            if (w.length >= 2) { return '(' + expr(w[1].text, warnings, lineNo) + ')'; }
            break;
        }
        warnings.push('line ' + lineNo + ": unsupported [" + inner.trim() + ']');
        return '/* unsupported: [' + inner.trim().replace(/\*\//g, '* /') + '] */ undefined';
    }

    // Translate an [expr {...}] body: TCL operators -> JS, subs translated.
    var EXPR_OP = { eq: '===', ne: '!==', and: '&&', or: '||', not: '!' };
    function expr(body, warnings, lineNo) {
        var out = [], i = 0, n = body.length;
        while (i < n) {
            var c = body[i];
            if (c === ' ' || c === '\t' || c === '\n' || c === '\r') { out.push(' '); i++; continue; }
            if (c === '$') {
                i++; var name = '';
                while (i < n && /[A-Za-z0-9_:]/.test(body[i])) { name += body[i++]; }
                out.push(varRef(name)); continue;
            }
            if (c === '[') {
                var depth = 0, s = '';
                while (i < n) {
                    if (body[i] === '[') { depth++; }
                    else if (body[i] === ']') { depth--; }
                    s += body[i++];
                    if (depth === 0) { break; }
                }
                out.push(commandSub(s.slice(1, -1), warnings, lineNo)); continue;
            }
            if (c === '"') {
                var q = ''; i++;
                while (i < n && body[i] !== '"') { q += body[i++]; }
                i++; out.push(quote(q)); continue;
            }
            if (/[A-Za-z_]/.test(c)) {                  // bareword operator/keyword
                var wtxt = '';
                while (i < n && /[A-Za-z_0-9]/.test(body[i])) { wtxt += body[i++]; }
                out.push(EXPR_OP.hasOwnProperty(wtxt) ? EXPR_OP[wtxt] : quote(wtxt));
                continue;
            }
            // operators / punctuation pass through (== != < > <= >= ? : ( ) && ||)
            out.push(c); i++;
        }
        return out.join('').replace(/\s+/g, ' ').trim();
    }

    // ---- statement translation -----------------------------------------------
    function statement(words, warnings, lineNo) {
        if (!words.length) { return ''; }
        var cmd = words[0].text;
        var v = function (w) { return value(w, warnings, lineNo); };

        switch (cmd) {
        case 'set':
            if (words.length >= 3) {
                var name = words[1].text.replace(/^\$/, '');
                return varRef(name) + ' = ' + v(words[2]) + ';';
            }
            break;
        case 'incr': {
            var iv = varRef(words[1].text.replace(/^\$/, ''));
            var amt = (words.length >= 3) ? v(words[2]) : '1';
            return iv + ' = (' + iv + ' || 0) + ' + amt + ';';
        }
        case 'unset':
            if (words.length >= 2) { return 'delete ' + varRef(words[1].text.replace(/^\$/, '')) + ';'; }
            break;
        case 'table':
            if (words.length >= 2) {
                var sub = words[1].text;
                if (sub === 'incr' && words.length >= 3) { return 'ev.table.incr(' + v(words[2]) + ');'; }
                if (sub === 'set'  && words.length >= 4) {
                    var ttl = (words.length >= 5) ? ', ' + v(words[4]) : '';
                    return 'ev.table.set(' + v(words[2]) + ', ' + v(words[3]) + ttl + ');';
                }
                if (sub === 'delete' && words.length >= 3) { return 'ev.table.delete(' + v(words[2]) + ');'; }
                if ((sub === 'lookup' || sub === 'get') && words.length >= 3) {
                    return 'ev.table.get(' + v(words[2]) + ');';
                }
            }
            break;
        case 'pool':
            if (words.length >= 2) { return 'ev.selectUpstream(' + v(words[1]) + ');'; }
            break;
        case 'HTTP::header':
            if (words.length >= 4 && (words[1].text === 'insert' || words[1].text === 'replace')) {
                return 'ev.setResponseHeader(' + v(words[2]) + ', ' + v(words[3]) + ');';
            }
            break;
        case 'HTTP::respond': {
            var code = (words.length >= 2) ? v(words[1]) : '200';
            var body = "''";
            for (var k = 2; k + 1 < words.length; k++) {
                if (words[k].type === 'bare' && words[k].text === 'content') { body = v(words[k + 1]); }
            }
            return 'ev.respond(' + code + ', {}, ' + body + ');';
        }
        case 'HTTP::redirect':
            if (words.length >= 2) { return 'ev.redirect(' + v(words[1]) + ');'; }
            break;
        case 'log':
            // log <facility> <msg>  ->  nginx.log(5, <msg>)
            if (words.length >= 3) { return 'nginx.log(5, ' + v(words[2]) + ');'; }
            if (words.length >= 2) { return 'nginx.log(5, ' + v(words[1]) + ');'; }
            break;
        case 'reject':
        case 'TCP::close':
            return 'ev.reject();';
        }

        warnings.push('line ' + lineNo + ": unsupported command '" + cmd + "'");
        return '// unsupported: ' + words.map(function (w) { return w.text; }).join(' ');
    }

    // Rough 1-based line number of a substring, for warnings.
    function lineOf(src, needle) {
        var idx = src.indexOf(needle);
        if (idx < 0) { return 0; }
        return src.slice(0, idx).split('\n').length;
    }

    // ---- top level ------------------------------------------------------------
    function transpile(tcl) {
        var warnings = [], events = [], isStream = false;
        var handlers = [];
        var cmds = splitCommands(tcl);

        cmds.forEach(function (cmdStr) {
            var w = splitWords(cmdStr);
            if (!w.length) { return; }
            if (w[0].text !== 'when') {
                warnings.push("top-level command '" + w[0].text +
                              "' outside a when-block (ignored)");
                return;
            }
            if (w.length < 3 || w[2].type !== 'brace') {
                warnings.push("malformed when-block: " + cmdStr.slice(0, 40));
                return;
            }
            var tclEvent = w[1].text;
            var mEvent = EVENT_MAP[tclEvent];
            if (!mEvent) {
                warnings.push("unsupported event '" + tclEvent + "' (no mirror mapping)");
                return;
            }
            if (STREAM_EVENTS[mEvent]) { isStream = true; }
            if (events.indexOf(mEvent) < 0) { events.push(mEvent); }

            var baseLine = lineOf(tcl, tclEvent);
            var body = splitCommands(w[2].text).map(function (s) {
                return '        ' + statement(splitWords(s), warnings, baseLine);
            }).filter(function (s) { return s.trim().length; });

            handlers.push('    ' + mEvent + ': function (ev) {\n' +
                          body.join('\n') + '\n    }');
        });

        return {
            events:   events,
            isStream: isStream,
            warnings: warnings,
            handlers: '{\n' + handlers.join(',\n') + '\n}'
        };
    }

    if (typeof globalThis !== 'undefined' && globalThis.mirror) {
        globalThis.mirror.transpile = transpile;
    }
    if (typeof globalThis !== 'undefined') {
        globalThis.mirrorTranspile = transpile;
    }
})();
