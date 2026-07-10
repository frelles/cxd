# CXD Library Guide

CXD stands for Component Extensible Data, a standalone C++ parser. CXD the Simplex structured data
format. Use it when a host tool needs to load configuration, manifests, test
fixtures, or embedded `data { }`-style content into a JSON-like tree.

This is practical usage documentation. For exact language rules, see
`docs/[CXD] Specification.md`.

## Build Target

```cmake
add_subdirectory(path/to/simplex/clib/cxd)
target_link_libraries(my_tool PRIVATE cxd)
```

```cpp
#include <cxd/cxd.hpp>
```

## Parse a String

```cpp
#include <cxd/cxd.hpp>
#include <iostream>

int main() {
    auto doc = cxd::parse(R"(
        @type:tool.config

        host: localhost,
        port: 8080,
        features: [logging, metrics],
    )");

    const cxd::Value* host = doc.root.get("host");
    const cxd::Value* port = doc.root.get("port");

    if (host && host->isString()) std::cout << host->string << "\n";
    if (port && port->isNumber()) std::cout << port->number << "\n";
}
```

`parse()` returns a `cxd::Document`:

- `doc.type` is the optional `@type:name` header.
- `doc.root` is a `cxd::Value`.

## Parse a File

```cpp
try {
    cxd::Document doc = cxd::parseFile("config/app.cxd");
    if (doc.root.has("database")) {
        // ...
    }
} catch (const cxd::ParseError& e) {
    std::cerr << e.what() << " at " << e.line << ":" << e.col << "\n";
}
```

## Value Types

`cxd::Value` is a tagged value:

| Kind          | Check        | Data member |
| ------------- | ------------ | ----------- |
| Null          | `isNull()`   |             |
| Bool          | `isBool()`   | `boolean`   |
| Number        | `isNumber()` | `number`    |
| String        | `isString()` | `string`    |
| Array         | `isArray()`  | `array`     |
| Object        | `isObject()` | `object`    |

Objects preserve insertion order and are stored as
`std::vector<std::pair<std::string, Value>>`.

Common helpers:

```cpp
const cxd::Value* name = doc.root.get("name");
bool hasPort = doc.root.has("port");
doc.root.set("enabled", cxd::Value::bool_val(true));
```

## Line Capture

CXD can capture non-standard value text as a string:

```cpp
auto doc = cxd::parse("query: SELECT id FROM users WHERE active = 1,\n");
const cxd::Value* query = doc.root.get("query");

if (query && query->isLineCapture) {
    // query->string == "SELECT id FROM users WHERE active = 1"
}
```

This is useful for host DSLs where CXD owns the outer structure but the value is
interpreted by another layer.

## Parse Options

Use `ParseOptions` when embedding CXD in another syntax.

```cpp
cxd::ParseOptions opts;
opts.allowTypeHeader = false;

auto doc = cxd::parse("name: Acme,\n", "<inline>", opts);
```

Available options:

- `allowTypeHeader`: accept or reject a leading `@type:`.
- `keyMode`: choose standard CXD keys or ECMAScript-style identifiers.
- `keyValidator`: provide a host-specific key scanner.
- `allowPropertyKeys`: allow `@name` as object keys.

The library no longer ships any domain-specific key validator. If a host format
needs special keys, keep that validator in the host layer and pass it through
`ParseOptions`.

## JSON Output

```cpp
std::string json = cxd::toJson(doc.root, 2);
```

`toJson()` serializes the value tree for diagnostics, tests, or interoperability.

## Error Handling

Invalid input throws `cxd::ParseError`.

```cpp
try {
    cxd::parse("{ key val }");
} catch (const cxd::ParseError& e) {
    std::cerr << e.what() << " at line " << e.line << "\n";
}
```

## Test

```bat
cmake -S . -B build\Debug -DCXD_BUILD_TESTS=ON
cmake --build build\Debug --config Debug --target cxd_tests
build\Debug\stage0bootstrap\clib\cxd\Debug\cxd_tests.exe
```
