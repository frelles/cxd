const CXD = require('./cxd.js');

let pass = 0, fail = 0;
function test(name, src, expected, options) {
    try {
        const result = CXD.parse(src, options);
        const actual = JSON.stringify(result.data);
        const exp = JSON.stringify(expected);
        if (actual === exp) {
            pass++;
        } else {
            fail++;
            console.log('FAIL:', name);
            console.log('  expected:', exp);
            console.log('  got:     ', actual);
        }
    } catch (e) {
        fail++;
        console.log('ERROR:', name, e.message);
    }
}

// §9.1 Application config
test('§9.1 app config',
`name: "Acme API",
version: 3,
env: production,
debug: false,

server: {
  host: "0.0.0.0",
  port: 8080,
  tls: true,
  timeout: 30,
},

routes: @[method, path, handler, auth] [
  ["GET",    "/users",     "listUsers",   true],
  ["POST",   "/users",     "createUser",  true],
  ["GET",    "/health",    "healthCheck", false],
  ["DELETE", "/users/:id", "deleteUser",  true],
],

tags: ["stable", "v3", "production"],`,
{
    name: "Acme API", version: 3, env: "production", debug: false,
    server: { host: "0.0.0.0", port: 8080, tls: true, timeout: 30 },
    routes: [
        { method: "GET", path: "/users", handler: "listUsers", auth: true },
        { method: "POST", path: "/users", handler: "createUser", auth: true },
        { method: "GET", path: "/health", handler: "healthCheck", auth: false },
        { method: "DELETE", path: "/users/:id", handler: "deleteUser", auth: true },
    ],
    tags: ["stable", "v3", "production"]
});

// §9.2 Anchors and spread
test('§9.2 anchors/spread',
`&base = {
  timeout: 30,
  retries: 3,
  tls: true,
  log-level: info,
},

development: {
  ...base,
  host: localhost,
  port: 3000,
  tls: false,
  log-level: verbose,
},

staging: {
  ...base,
  host: "staging.example.com",
  port: 443,
},

production: {
  ...base,
  host: "api.example.com",
  port: 443,
  cdn: "cdn.example.com",
},`,
{
    development: { timeout: 30, retries: 3, tls: false, "log-level": "verbose", host: "localhost", port: 3000 },
    staging: { timeout: 30, retries: 3, tls: true, "log-level": "info", host: "staging.example.com", port: 443 },
    production: { timeout: 30, retries: 3, tls: true, "log-level": "info", host: "api.example.com", port: 443, cdn: "cdn.example.com" },
});

// §9.3 Extended numbers
test('§9.3 extended numbers',
`permissions: {
  read:    0b0001,
  write:   0b0010,
  execute: 0b0100,
  admin:   0xFF,
  default: 0o644,
},

limits: {
  max-connections: 1 000 000,
  max-payload:     10 485 760,
  rate-limit:      1 000,
  pi:              3.141 592 653 589 793,
},`,
{
    permissions: { read: 1, write: 2, execute: 4, admin: 255, default: 420 },
    limits: { "max-connections": 1000000, "max-payload": 10485760, "rate-limit": 1000, pi: 3.141592653589793 },
});

// §9.4 Triple-quoted strings
test('§9.4 triple strings',
`name: "my-service",
version: "2.0.0",
license: MIT,

description: """
  A lightweight HTTP service for user management.
  Supports OAuth2, RBAC, and multi-tenancy out of the box.
""",`,
{
    name: "my-service", version: "2.0.0", license: "MIT",
    description: "A lightweight HTTP service for user management.\nSupports OAuth2, RBAC, and multi-tenancy out of the box."
});

// §9.5 Nested anchors
test('§9.5 nested anchors',
`&db-defaults = {
  port: 5432,
  pool-size: 10,
  ssl: true,
},

databases: {
  primary: {
    ...db-defaults,
    host: "db-primary.internal",
    name: "acme_prod",
  },
  replica: {
    ...db-defaults,
    host: "db-replica.internal",
    name: "acme_prod",
    pool-size: 20,
  },
  analytics: {
    ...db-defaults,
    host: "db-analytics.internal",
    name: "acme_analytics",
    port: 5433,
    ssl: false,
  },
},`,
{
    databases: {
        primary: { port: 5432, "pool-size": 10, ssl: true, host: "db-primary.internal", name: "acme_prod" },
        replica: { port: 5432, "pool-size": 10, ssl: true, host: "db-replica.internal", name: "acme_prod", "pool-size": 20 },
        analytics: { port: 5432, "pool-size": 10, ssl: true, host: "db-analytics.internal", name: "acme_analytics", port: 5433, ssl: false },
    }
});

// @type declaration
test('@type declaration',
`@type:archetype

manifestName: acmeCoreEngine,
engineVersion: "4.2.001",`,
{ manifestName: "acmeCoreEngine", engineVersion: "4.2.001" });

// Line capture
test('line capture',
`sidebar: @col ~240 =f5f5f5 |r0 +16 *0 ^1,
query: SELECT id FROM users WHERE active = 1,`,
{ sidebar: "@col ~240 =f5f5f5 |r0 +16 *0 ^1", query: "SELECT id FROM users WHERE active = 1" });

// Single values
test('array value', `[1, 2, 3]`, [1, 2, 3]);
test('string value', `"hello"`, "hello");
test('number value', `42`, 42);
test('boolean', `true`, true);
test('null', `null`, null);

// Empty object
test('empty implicit', ``, {});

// Comments
test('comments',
`# top comment
name: "test", # inline
value: 42,`,
{ name: "test", value: 42 });

// Bare strings
test('bare strings',
`env: production,
region: us-east-1,`,
{ env: "production", region: "us-east-1" });

// Described list with short rows
test('described list short row',
`items: @[a, b, c] [[1, 2]]`,
{ items: [{ a: 1, b: 2, c: null }] });

// Hex color in line capture
test('hex color line capture',
`color: #FF0000,
next: ok,`,
{ color: "#FF0000", next: "ok" });

console.log('\n' + pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
