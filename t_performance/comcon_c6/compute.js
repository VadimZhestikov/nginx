onRequest(function(req) {
    var s = 0;
    for (var i = 0; i < 20000; i++) { s = (s + i * 3) | 0; }
    return "sum=" + s + "\n";
});
