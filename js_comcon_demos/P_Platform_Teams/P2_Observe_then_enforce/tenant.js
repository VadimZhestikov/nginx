// THE TENANT — holds a leased socket capability `s` and reads two fields.
//
// The host bound this SAME text twice: once with onViolation: "audit" (the
// shadow binding: a violation is logged and ALLOWED) and once with
// onViolation: "deny".  The lease is one second long, so by the time the demo
// asks, every read is a violation -- and the two bindings answer differently.

export default function (req) {
    return {
        addressType: typeof s.address,      // "string" when allowed, "undefined" when denied
        portType:    typeof s.port,
        note:        "same tenant text, two postures"
    };
}
