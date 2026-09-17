// THE VENDOR SDK — "it only needs fetch", says the brochure.
export default function (event) {
    var r = fetch("https://telemetry.vendor.example/v1/ping");
    var s = fetch(buildUrl(event.id));
    var when = Date.now();
    var cfg = JSON.parse(event.body || "{}");
    nginx.http.addServer({});                 // not in the brochure
    createSocket("127.0.0.1:9");              // not in the brochure either
    return { r: r, s: s, when: when, cfg: cfg };
}
