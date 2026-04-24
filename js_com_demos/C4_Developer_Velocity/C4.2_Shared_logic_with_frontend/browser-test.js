// browser-test.js — simulates browser-context testing of normalize.js
// Run with: qjs browser-test.js
//
// In a real project this file would be:
//   - Executed by a browser test runner (Vitest, Jest with jsdom)
//   - Run by qjs in CI as a fast smoke test
// The logic is identical in both environments because normalize.js
// has no platform dependencies.

import { normalizeUrl } from './normalize.js';

var pass = 0, fail = 0;

function check(desc, expected, actual) {
    if (expected === actual) {
        print('PASS: ' + desc);
        pass++;
    } else {
        print('FAIL: ' + desc +
              '\n       expected: ' + JSON.stringify(expected) +
              '\n       got:      ' + JSON.stringify(actual));
        fail++;
    }
}

// Basic normalization
check('lowercase',
    '/api/users',
    normalizeUrl('/API/Users'));

check('collapse double slashes',
    '/api/users',
    normalizeUrl('/api//users'));

check('collapse many slashes',
    '/api/users/profile',
    normalizeUrl('/API//Users//Profile'));

check('remove trailing slash',
    '/api/users',
    normalizeUrl('/api/users/'));

check('keep lone slash',
    '/',
    normalizeUrl('/'));

check('empty string → /',
    '/',
    normalizeUrl(''));

check('null → /',
    '/',
    normalizeUrl(null));

// Special character replacement
check('spaces replaced with _',
    '/api/my_endpoint',
    normalizeUrl('/api/my endpoint'));

check('query-string chars replaced',
    '/api/search_q_foo',
    normalizeUrl('/api/search?q=foo'));

check('hash replaced',
    '/page_section',
    normalizeUrl('/page#section'));

// Dots and hyphens preserved
check('dots preserved in filenames',
    '/static/app.js',
    normalizeUrl('/static/app.js'));

check('hyphens preserved',
    '/api/user-profile',
    normalizeUrl('/api/user-profile'));

check('underscores preserved',
    '/api/snake_case',
    normalizeUrl('/api/snake_case'));

// Combined
check('complex path fully normalized',
    '/api/v1/user_profile',
    normalizeUrl('/API//v1//User Profile'));

print('');
print('[browser-test] Results: ' + pass + ' passed, ' + fail + ' failed');
print('[browser-test] Same function works in nginx AND browser contexts.');
if (fail > 0) std.exit(1);
