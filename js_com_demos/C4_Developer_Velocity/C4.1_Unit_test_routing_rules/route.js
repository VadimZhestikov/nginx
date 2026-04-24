// route.js — pure routing logic (no platform dependencies)
// Works identically in QuickJS (qjs), nginx handler, and browsers.

export function matchRoute(uri, headers) {
    var accept = (headers && headers['accept']) || '';

    // API routes
    if (uri.startsWith('/api/')) {
        if (accept && accept.indexOf('json') !== -1)
            return 'json_backend';
        return 'api_backend';
    }

    // Static assets
    if (uri.startsWith('/static/'))
        return 'cdn_backend';

    // Health checks
    if (uri === '/health' || uri === '/health/')
        return 'health_backend';

    // Admin area — requires explicit header
    if (uri.startsWith('/admin/')) {
        if (headers && headers['x-internal'] === 'true')
            return 'admin_backend';
        return 'forbidden';
    }

    return 'default_backend';
}
