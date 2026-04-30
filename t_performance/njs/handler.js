function empty(r) {
    r.return(200, 'ok\n');
}

function headers(r) {
    var out = '';
    for (var k in r.headersIn) {
        out += k + ': ' + r.headersIn[k] + '\n';
    }
    r.return(200, out);
}

export default { empty, headers };
