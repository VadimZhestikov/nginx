// normalize.js — URL normalization (no platform APIs)
// Works identically in QuickJS (nginx + qjs) and browsers.
// Import as ES module or inline the function.

export function normalizeUrl(url) {
    if (!url || typeof url !== 'string') return '/';

    // Lowercase
    var result = url.toLowerCase();

    // Collapse consecutive slashes
    result = result.replace(/\/+/g, '/');

    // Remove trailing slash (but keep lone /)
    if (result.length > 1) result = result.replace(/\/$/, '');

    // Replace characters that are not alphanumeric, slash, hyphen, underscore, dot
    result = result.replace(/[^a-z0-9\/\-_\.]/g, '_');

    return result || '/';
}
