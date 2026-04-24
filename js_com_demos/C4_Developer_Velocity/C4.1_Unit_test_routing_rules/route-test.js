// route-test.js — unit tests for route.js, runs with: qjs route-test.js
import { matchRoute } from './route.js';

var pass = 0, fail = 0;

function check(desc, expected, actual) {
    if (expected === actual) {
        print('PASS: ' + desc);
        pass++;
    } else {
        print('FAIL: ' + desc +
              '\n       expected: ' + expected +
              '\n       got:      ' + actual);
        fail++;
    }
}

// API routes
check('/api/ with JSON accept → json_backend',
    'json_backend',
    matchRoute('/api/users', { 'accept': 'application/json' }));

check('/api/ with json in accept → json_backend',
    'json_backend',
    matchRoute('/api/items', { 'accept': 'text/html, application/json' }));

check('/api/ without JSON accept → api_backend',
    'api_backend',
    matchRoute('/api/users', {}));

check('/api/ with no headers → api_backend',
    'api_backend',
    matchRoute('/api/data', null));

check('/api/nested/path → api_backend',
    'api_backend',
    matchRoute('/api/v2/users/123', {}));

// Static
check('/static/ → cdn_backend',
    'cdn_backend',
    matchRoute('/static/logo.png', {}));

check('/static/ with JSON accept still → cdn_backend',
    'cdn_backend',
    matchRoute('/static/app.js', { 'accept': 'application/json' }));

// Health check
check('/health → health_backend',
    'health_backend',
    matchRoute('/health', {}));

check('/health/ → health_backend',
    'health_backend',
    matchRoute('/health/', {}));

// Admin
check('/admin/ with x-internal:true → admin_backend',
    'admin_backend',
    matchRoute('/admin/settings', { 'x-internal': 'true' }));

check('/admin/ without x-internal → forbidden',
    'forbidden',
    matchRoute('/admin/settings', {}));

// Default
check('/ → default_backend',
    'default_backend',
    matchRoute('/', {}));

check('/unknown/path → default_backend',
    'default_backend',
    matchRoute('/unknown/path', {}));

print('');
print('Results: ' + pass + ' passed, ' + fail + ' failed');
if (fail > 0) std.exit(1);
