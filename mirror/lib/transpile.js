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
        case 'HTTP::uri':
        case 'HTTP::path':   return 'ev.uri';
        case 'HTTP::method': return 'ev.method';
        case 'HTTP::host':   return 'ev.header("host")';
        case 'HTTP::header':
            if (w.length >= 2) { return 'ev.header(' + value(w[1], warnings, lineNo) + ')'; }
            break;
        case 'HTTP::cookie':
            if (w.length >= 2) { return 'ev.cookie(' + value(w[1], warnings, lineNo) + ')'; }
            break;
        case 'string': {
            var s0 = (w.length >= 3) ? value(w[2], warnings, lineNo) : '""';
            switch (w[1] && w[1].text) {
            case 'tolower':   return 'String(' + s0 + ').toLowerCase()';
            case 'toupper':   return 'String(' + s0 + ').toUpperCase()';
            case 'length':    return 'String(' + s0 + ').length';
            case 'trim':      return 'String(' + s0 + ').trim()';
            case 'trimleft':  return 'String(' + s0 + ').replace(/^\\s+/, "")';
            case 'trimright': return 'String(' + s0 + ').replace(/\\s+$/, "")';
            case 'range':     // string range S first last  ->  slice(first, last+1)
                if (w.length >= 5) {
                    return 'String(' + s0 + ').slice(' + value(w[3], warnings, lineNo) +
                           ', ' + value(w[4], warnings, lineNo) + ' + 1)';
                }
                break;
            }
            break;
        }
        case 'substr':       // substr S start [length]
            if (w.length >= 3) {
                var st = value(w[2], warnings, lineNo);
                var ln = (w.length >= 4) ? ', ' + value(w[3], warnings, lineNo) : '';
                return 'String(' + value(w[1], warnings, lineNo) + ').substr(' + st + ln + ')';
            }
            break;
        case 'table':
            if (w.length >= 3 && (w[1].text === 'lookup' || w[1].text === 'get')) {
                return 'ev.table.get(' + value(w[2], warnings, lineNo) + ')';
            }
            break;
        case 'class':
            // class match <subject> <op> <class>   (op: equals/contains/...)
            // class match <subject> <class>         (equals implied)
            // class lookup <key> <class>
            if (w[1] && w[1].text === 'match' && w.length >= 5) {
                return 'mirror.classMatch(' + value(w[4], warnings, lineNo) + ', ' +
                       quote(w[3].text) + ', ' + value(w[2], warnings, lineNo) + ')';
            }
            if (w[1] && w[1].text === 'match' && w.length === 4) {
                return 'mirror.classMatch(' + value(w[3], warnings, lineNo) +
                       ', "equals", ' + value(w[2], warnings, lineNo) + ')';
            }
            if (w[1] && w[1].text === 'lookup' && w.length >= 4) {
                return 'mirror.classLookup(' + value(w[3], warnings, lineNo) + ', ' +
                       value(w[2], warnings, lineNo) + ')';
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
    // Word operators that map to a JS infix operator:
    var EXPR_OP = { eq: '===', ne: '!==', equals: '===', and: '&&', or: '||', not: '!' };
    // Binary string operators that map to a JS method call (folded below):
    var EXPR_STROP = {
        contains:    function (l, r) { return '(String(' + l + ').includes(' + r + '))'; },
        starts_with: function (l, r) { return '(String(' + l + ').startsWith(' + r + '))'; },
        ends_with:   function (l, r) { return '(String(' + l + ').endsWith(' + r + '))'; }
    };

    // Tokenize an expr body into {k:'val'|'op'|'strop', v} tokens.
    function tokenizeExpr(body, warnings, lineNo) {
        var toks = [], i = 0, n = body.length;
        while (i < n) {
            var c = body[i];
            if (c === ' ' || c === '\t' || c === '\n' || c === '\r') { i++; continue; }
            if (c === '$') {
                i++; var name = '';
                while (i < n && /[A-Za-z0-9_:]/.test(body[i])) { name += body[i++]; }
                toks.push({ k: 'val', v: varRef(name) }); continue;
            }
            if (c === '[') {
                var depth = 0, s = '';
                while (i < n) {
                    if (body[i] === '[') { depth++; }
                    else if (body[i] === ']') { depth--; }
                    s += body[i++];
                    if (depth === 0) { break; }
                }
                toks.push({ k: 'val', v: commandSub(s.slice(1, -1), warnings, lineNo) }); continue;
            }
            if (c === '"') {
                var q = ''; i++;
                while (i < n && body[i] !== '"') { q += body[i++]; }
                i++; toks.push({ k: 'val', v: quote(q) }); continue;
            }
            if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(body[i + 1] || ''))) {
                var num = '';
                while (i < n && /[0-9.]/.test(body[i])) { num += body[i++]; }
                toks.push({ k: 'val', v: num }); continue;
            }
            if (/[A-Za-z_]/.test(c)) {                  // bareword: operator or keyword
                var wtxt = '';
                while (i < n && /[A-Za-z_0-9]/.test(body[i])) { wtxt += body[i++]; }
                if (EXPR_STROP.hasOwnProperty(wtxt)) { toks.push({ k: 'strop', v: wtxt }); }
                else if (EXPR_OP.hasOwnProperty(wtxt)) { toks.push({ k: 'op', v: EXPR_OP[wtxt] }); }
                else if (wtxt === 'true' || wtxt === 'false') { toks.push({ k: 'val', v: wtxt }); }
                else {
                    warnings.push('line ' + lineNo + ": unknown expr word '" + wtxt +
                                  "' (treated as a string literal)");
                    toks.push({ k: 'val', v: quote(wtxt) });
                }
                continue;
            }
            // multi-char / single-char symbol operators
            var two = body.substr(i, 2);
            if (two === '==' || two === '!=' || two === '<=' || two === '>=' ||
                two === '&&' || two === '||') {
                toks.push({ k: 'op', v: (two === '==' ? '===' : two === '!=' ? '!==' : two) });
                i += 2; continue;
            }
            toks.push({ k: 'op', v: c }); i++;          // < > ! ? : ( ) + - * / %
        }
        return toks;
    }

    function expr(body, warnings, lineNo) {
        var toks = tokenizeExpr(body, warnings, lineNo);
        var out = [];
        for (var i = 0; i < toks.length; i++) {
            var t = toks[i];
            if (t.k === 'strop') {
                var left = out.length ? out.pop() : '""';
                var rtok = toks[i + 1];
                var right = (rtok && rtok.k === 'val') ? rtok.v : '""';
                if (rtok && rtok.k === 'val') { i++; }
                out.push(EXPR_STROP[t.v](left, right));
            } else {
                out.push(t.v);
            }
        }
        return out.join(' ').replace(/\s+/g, ' ').trim();
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

    // ---- control flow (blocks) -----------------------------------------------
    // Translate a brace body into indented, newline-joined mirror statements.
    function block(bodyText, warnings, lineNo, indent) {
        return splitCommands(bodyText).map(function (s) {
            return dispatch(splitWords(s), warnings, lineNo, indent);
        }).filter(function (s) { return s.replace(/\s/g, '').length; }).join('\n');
    }

    // Statement dispatcher: control-flow blocks emit fully-indented multi-line
    // text; every other command is a single line prefixed with `indent`.
    function dispatch(words, warnings, lineNo, indent) {
        if (!words.length) { return ''; }
        switch (words[0].text) {
        case 'if':      return ifBlock(words, warnings, lineNo, indent);
        case 'switch':  return switchBlock(words, warnings, lineNo, indent);
        case 'foreach': return foreachBlock(words, warnings, lineNo, indent);
        default:        return indent + statement(words, warnings, lineNo);
        }
    }

    // if {c} [then] {b} [elseif {c} {b}]* [else {b}]
    function ifBlock(words, warnings, lineNo, indent) {
        var body = indent + '    ';
        var idx = 1, clauses = [];

        function grabBody() {                    // consume optional `then`, then a brace
            if (words[idx] && words[idx].type === 'bare' && words[idx].text === 'then') { idx++; }
            var b = words[idx++];
            if (!b || b.type !== 'brace') {
                warnings.push('line ' + lineNo + ': if-branch body must be a { block }');
                return null;
            }
            return b.text;
        }

        var cond = words[idx++];
        if (!cond || cond.type !== 'brace') {
            warnings.push('line ' + lineNo + ': malformed if condition');
            return indent + '// unsupported: if ...';
        }
        var b0 = grabBody();
        if (b0 === null) { return indent + '// unsupported: if ...'; }
        clauses.push({ cond: expr(cond.text, warnings, lineNo), body: b0 });

        while (words[idx] && words[idx].type === 'bare' &&
               (words[idx].text === 'elseif' || words[idx].text === 'else')) {
            if (words[idx].text === 'elseif') {
                idx++;
                var ec = words[idx++];
                var eb = grabBody();
                if (eb === null) { break; }
                clauses.push({ cond: expr(ec.text, warnings, lineNo), body: eb });
            } else {                              // else
                idx++;
                var elb = words[idx++];
                if (!elb || elb.type !== 'brace') {
                    warnings.push('line ' + lineNo + ': else body must be a { block }');
                    break;
                }
                clauses.push({ cond: null, body: elb.text });
                break;
            }
        }

        var out = '';
        clauses.forEach(function (c, i) {
            var head;
            if (i === 0)            { head = indent + 'if (' + c.cond + ') {'; }
            else if (c.cond !== null) { head = ' else if (' + c.cond + ') {'; }
            else                    { head = ' else {'; }
            out += (i === 0 ? head : head) + '\n' +
                   block(c.body, warnings, lineNo, body) + '\n' +
                   (i === 0 ? indent + '}' : indent + '}');
        });
        return out;
    }

    // switch [-exact|-glob|--]* $val { pat {body} ... default {body} }
    // First cut: exact string matching -> a JS switch (no fall-through). -glob
    // is warned (matched as exact).
    function switchBlock(words, warnings, lineNo, indent) {
        var idx = 1;
        while (words[idx] && words[idx].type === 'bare' &&
               words[idx].text.charAt(0) === '-') {
            if (words[idx].text === '-glob') {
                warnings.push('line ' + lineNo + ': switch -glob matched as exact (glob not supported)');
            }
            var stop = (words[idx].text === '--');
            idx++;
            if (stop) { break; }
        }
        var valWord = words[idx++];
        var pairsWord = words[idx++];
        if (!valWord || !pairsWord || pairsWord.type !== 'brace') {
            warnings.push('line ' + lineNo + ': malformed switch');
            return indent + '// unsupported: switch ...';
        }
        var valExpr = value(valWord, warnings, lineNo);
        var pw = splitWords(pairsWord.text);
        var inner = indent + '    ', innerBody = indent + '        ';
        var out = indent + 'switch (' + valExpr + ') {';
        for (var i = 0; i + 1 < pw.length; i += 2) {
            var pat = pw[i], bodyW = pw[i + 1];
            if (bodyW.type !== 'brace') {
                warnings.push('line ' + lineNo + ': switch case body must be a { block }');
                continue;
            }
            if (pat.type === 'bare' && pat.text === 'default') {
                out += '\n' + inner + 'default: {\n' +
                       block(bodyW.text, warnings, lineNo, innerBody) + '\n' +
                       innerBody + 'break;\n' + inner + '}';
            } else {
                out += '\n' + inner + 'case ' + value(pat, warnings, lineNo) + ': {\n' +
                       block(bodyW.text, warnings, lineNo, innerBody) + '\n' +
                       innerBody + 'break;\n' + inner + '}';
            }
        }
        out += '\n' + indent + '}';
        return out;
    }

    // foreach var {a b c} {body}  ->  forEach over a literal list.
    // Command-substituted / variable lists are warned (not supported yet).
    function foreachBlock(words, warnings, lineNo, indent) {
        if (words.length < 4 || words[2].type !== 'brace' || words[3].type !== 'brace') {
            warnings.push('line ' + lineNo + ': foreach supports only a literal { list } (skipped)');
            return indent + '// unsupported: foreach ' +
                   words.slice(1).map(function (w) { return w.text; }).join(' ');
        }
        var vname = words[1].text.replace(/^\$/, '');
        var items = words[2].text.trim().split(/\s+/)
                        .filter(function (t) { return t.length; })
                        .map(function (t) { return quote(t); });
        var inner = indent + '    ';
        return indent + '[' + items.join(', ') + '].forEach(function (_it) {\n' +
               inner + varRef(vname) + ' = _it;\n' +
               block(words[3].text, warnings, lineNo, inner) + '\n' +
               indent + '});';
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
            var body = block(w[2].text, warnings, baseLine, '        ');

            handlers.push('    ' + mEvent + ': function (ev) {\n' +
                          body + '\n    }');
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
