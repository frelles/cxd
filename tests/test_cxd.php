<?php
require_once __DIR__ . '/cxd.php';

$pass = 0; $fail = 0;

function test($name, $src, $expected, $options = []) {
    global $pass, $fail;
    try {
        $result = CXD::parse($src, $options);
        $actual = json_encode($result['data']);
        $exp = json_encode($expected);
        if ($actual === $exp) {
            $pass++;
        } else {
            $fail++;
            echo "FAIL: $name\n  expected: $exp\n  got:      $actual\n";
        }
    } catch (Exception $e) {
        $fail++;
        echo "ERROR: $name — {$e->getMessage()}\n";
    }
}

// §9.1 App config
test('§9.1 app config',
'name: "Acme API",
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

tags: ["stable", "v3", "production"],',
[
    'name' => 'Acme API', 'version' => 3, 'env' => 'production', 'debug' => false,
    'server' => ['host' => '0.0.0.0', 'port' => 8080, 'tls' => true, 'timeout' => 30],
    'routes' => [
        ['method' => 'GET', 'path' => '/users', 'handler' => 'listUsers', 'auth' => true],
        ['method' => 'POST', 'path' => '/users', 'handler' => 'createUser', 'auth' => true],
        ['method' => 'GET', 'path' => '/health', 'handler' => 'healthCheck', 'auth' => false],
        ['method' => 'DELETE', 'path' => '/users/:id', 'handler' => 'deleteUser', 'auth' => true],
    ],
    'tags' => ['stable', 'v3', 'production']
]);

// §9.2 Anchors/spread
test('§9.2 anchors/spread',
'&base = {
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
},',
[
    'development' => ['timeout' => 30, 'retries' => 3, 'tls' => false, 'log-level' => 'verbose', 'host' => 'localhost', 'port' => 3000],
]);

// §9.3 Extended numbers
test('§9.3 extended numbers',
'permissions: {
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
},',
[
    'permissions' => ['read' => 1, 'write' => 2, 'execute' => 4, 'admin' => 255, 'default' => 420],
    'limits' => ['max-connections' => 1000000, 'max-payload' => 10485760, 'rate-limit' => 1000],
]);

// §9.4 Triple strings
test('§9.4 triple strings',
'name: "my-service",
description: """
  A lightweight HTTP service.
  Supports OAuth2 and RBAC.
""",',
[
    'name' => 'my-service',
    'description' => "A lightweight HTTP service.\nSupports OAuth2 and RBAC."
]);

// @type
test('@type declaration',
'@type:archetype

manifestName: acmeCoreEngine,',
['manifestName' => 'acmeCoreEngine']);

// Line capture
test('line capture',
'sidebar: @col ~240 =f5f5f5 |r0 +16 *0 ^1,
query: SELECT id FROM users WHERE active = 1,',
['sidebar' => '@col ~240 =f5f5f5 |r0 +16 *0 ^1', 'query' => 'SELECT id FROM users WHERE active = 1']);

// Bare strings
test('bare strings', 'env: production, region: us-east-1,',
['env' => 'production', 'region' => 'us-east-1']);

// Hex color
test('hex color line capture', "color: #FF0000,\nnext: ok,",
['color' => '#FF0000', 'next' => 'ok']);

// Simple values
test('array', '[1, 2, 3]', [1, 2, 3]);
test('number', '42', 42);
test('boolean', 'true', true);
test('null', 'null', null);
test('empty', '', []);

echo "\n$pass passed, $fail failed\n";
exit($fail ? 1 : 0);
