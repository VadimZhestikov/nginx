// THE TENANT — reads the socket's port, once per request.  Under the
// candidate policy that read is BUDGETED (two per hour): the third request
// on is a denial the current policy never had.
export default function (req) {
    var seen = [];
    if (typeof s.port === "number") { seen.push("port"); }
    return { seen: seen };
}
