// A1.1 — Live SSL Certificate Rotation
//
// In a production setup the server on port 8100 would be declared with:
//   listen 8100 ssl;
//   ssl_certificate     /etc/nginx/certs/current.crt;
//   ssl_certificate_key /etc/nginx/certs/current.key;
//
// Then server.ssl.setCertificate(certPEM, keyPEM) swaps the certificate
// and key in memory — all NEW TLS handshakes use the new material
// immediately.  In-flight connections keep the old cert.  Zero downtime,
// no reload required.
//
// This demo runs an HTTP server on port 8101 so it can be tested without
// openssl tooling.  It simulates the cert-rotation workflow via an admin
// endpoint and an in-memory "active cert version" variable.

(function () {
    var servers = nginx.http.servers;

    // Simulated state: which cert version is currently active
    var certVersion = 'v1-initial';

    // In a real setup you would find the SSL server like this:
    //   var sslServer = servers.find(function(s) {
    //       return s.name === 'secure.example.com';
    //   });
    //   sslServer.ssl.setCertificate(newCertPEM, newKeyPEM);

    var adminServer = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/admin/cert/'; });
    });

    var locs = adminServer.locations;

    // POST /admin/cert/ — simulate hot-swap of TLS certificate
    locs.find(function (l) { return l.path === '/admin/cert/'; })
        .handler = function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            // In production this would be:
            //   var certPEM = r.headers['x-cert-pem'];
            //   var keyPEM  = r.headers['x-key-pem'];
            //   sslServer.ssl.setCertificate(certPEM, keyPEM);

            var requestedVersion = r.headers['x-cert-version'] || 'v2-rotated';
            certVersion = requestedVersion;

            nginx.log(4, 'SSL cert rotated to ' + certVersion);
            r.respond(200, {}, 'Certificate rotated to ' + certVersion + '\n');
        };

    // GET /status/ — show active cert version
    locs.find(function (l) { return l.path === '/status/'; })
        .handler = function (r) {
            r.respond(200, {}, 'active-cert: ' + certVersion + '\n');
        };
})();
