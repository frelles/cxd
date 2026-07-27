# CXD Library (`cxd_c` / `cxd`)

**Component Extensible Data** — a standalone parser for `.cxd` files and CXD-shaped text. Use it when a host tool needs configuration, manifests, test fixtures, or embedded `data { }`-style content in a JSON-like tree.

| Item | Detail |
| ---- | ------ |
| C target   | `cxd_c`  |
| C++ target | `cxd` (bindings over `cxd_c`) |
| C header   | [`include/cxd/cxd.h`](include/cxd/cxd.h) |
| C++ header | [`../cpp/cxd/include/cxd/cxd.hpp`](/include/cxd/cxd.hpp) |
| Spec       | [`CXD Specification.md`](CXD%20Specification%20Specification.md) |

This guide covers **library usage**. Normative syntax lives in the spec.

---

## Build and link

From the repo (recommended):

```bat
cmake -S library/c -B build/c-lib -DCXD_BUILD_TESTS=ON
cmake --build build/c-lib --config Debug --target cxd_c
```

CMake in another project:

```cmake
add_subdirectory(path/to/simplex/library/c)
target_link_libraries(my_tool PRIVATE cxd_c)   # C
# or
target_link_libraries(my_tool PRIVATE cxd)     # C++
```

Manual C link (MSVC example):

```bat
cl /I path\to\library\c\cxd\include myapp.c path\to\library\c\cxd\src\cxd.c
```

---

## C API quick start

```c
#include "cxd/cxd.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char *src =
        "@type:app.config\n\n"
        "host: \"localhost\",\n"
        "port: 8080,\n";

    CxdDocument doc;
    char err[256];

    if (!cxd_parse(src, "app.cxd", NULL, &doc, err, sizeof(err))) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }

    if (doc.type) printf("type: %s\n", doc.type);

    const CxdValue *host = cxd_get(&doc.root, "host");
    const CxdValue *port = cxd_get(&doc.root, "port");

    if (host && host->kind == CXD_STRING)
        printf("host=%s\n", host->string);
    if (port && port->kind == CXD_NUMBER)
        printf("port=%.0f\n", port->number);

    cxd_free_document(&doc);
    return 0;
}
```

Parse a file:

```c
CxdDocument doc;
char err[256];
if (!cxd_parse_file("config/app.cxd", NULL, &doc, err, sizeof(err))) { /* ... */ }
```

Export JSON:

```c
char *json = NULL;
size_t len = 0;
if (cxd_to_json(&doc.root, &json, &len)) {
    printf("%s\n", json);
    free(json);
}
```

---

## C value model

`CxdValue` is a tagged tree node:

| `CxdKind`     | Payload fields                          |
| ------------- | --------------------------------------- |
| `CXD_NULL`    | —                                       |
| `CXD_BOOL`    | `boolean`                               |
| `CXD_NUMBER`  | `number` (IEEE double)                  |
| `CXD_STRING`  | `string` (heap-owned)                   |
| `CXD_ARRAY`   | `array`, `array_len`                    |
| `CXD_OBJECT`  | `object`, `object_len` (`CxdField` list)|

Every value carries source location: `line`, `col` (1-based).

**Line capture:** when a field value is parsed as raw line text, `is_line_capture` is true and `string` holds the captured line (without trailing comma/newline handling per spec).

Object helpers:

```c
const CxdValue *v = cxd_get(&doc.root, "database");
if (cxd_has(&doc.root, "enabled")) { /* ... */ }
CxdValue *mut = cxd_get_mut(&doc.root, "count");  /* in-place edit */
```

Objects preserve insertion order (linked field list, not a hash map).

---

## Parse options (C)

```c
CxdParseOptions opts;
cxd_options_default(&opts);
opts.allow_type_header = false;   /* embedded CXD inside another syntax */

cxd_parse("name: Acme,\n", "<inline>", &opts, &doc, err, sizeof(err));
```

| Field               | Default | Meaning |
| ------------------- | ------- | ------- |
| `allow_type_header` | `true`  | Accept leading `@type:name` document header |

When `allow_type_header` is `false`, a leading `@type:` line is a **syntax error** (use for inline `data { }` blocks).

---

## C++ API quick start

The C++ layer wraps the C library and throws on failure.

```cpp
#include <cxd/cxd.hpp>
#include <iostream>

int main() {
    try {
        auto doc = cxd::parse(R"(
            @type:tool.config

            host: localhost,
            port: 8080,
            features: [logging, metrics],
        )");

        if (doc.type) std::cout << *doc.type << "\n";

        if (const auto* host = doc.root.get("host"))
            if (host->isString()) std::cout << host->string << "\n";

        if (const auto* port = doc.root.get("port"))
            if (port->isNumber()) std::cout << port->number << "\n";

        std::cout << cxd::toJson(doc.root, 2) << "\n";
    } catch (const cxd::ParseError& e) {
        std::cerr << e.what() << "\n";
    }
}
```

File API:

```cpp
auto doc = cxd::parseFile("config/app.cxd");
```

`cxd::Value` mirrors the C tree with `std::vector` containers and `get` / `has` / `set` on objects.

---

## Supported CXD surface (this library)

Implemented in the current C parser:

- Implicit objects (`key: value,` at document root)
- Explicit `{ key: value, }` objects and `[ ... ]` arrays
- `@type:` document header (optional)
- Anchors (`&name = { ... }`), spread (`...name`), clone (`*name`)
- Described lists (`@[cols] [ rows ]`)
- Line capture values
- Numbers: decimal, grouped spaces, `0x` / `0b` / `0o`, scientific
- Strings: `"..."`, `'...'`, triple-quoted, bare identifiers as strings
- `#` line comments

**C++ `ParseOptions` fields** beyond `allowTypeHeader` (`keyMode`, `keyValidator`, `allowPropertyKeys`) are declared in the header for API compatibility but are **not wired through** to the C backend yet — only `allowTypeHeader` affects parsing today.

**Known gap:** triple-quoted string de-indentation may differ from the legacy all-C++ parser; see `library/c/cpp/tests/test_cxd.cpp` if you rely on that form.

---

## Memory and errors

**C:** all functions return `bool`. On failure, `cxd_parse` / `cxd_parse_file` write a message into `err` (if `err_cap > 0`). Always call `cxd_free_document` when done; it frees `type`, the root tree, and nested allocations.

**C++:** failures throw `cxd::ParseError` (message only; line/col are `0` until the C layer reports locations in `err`).

---

## Tests

```bat
cmake --build build/c-lib --config Debug --target cxd_c_tests cxd_tests
build\c-lib\cxd\Debug\cxd_c_tests.exe
build\c-lib\cpp\Debug\cxd_tests.exe
```

---

## See also

- [CXP library guide](../cxp/README.md)
- [Simplex C libraries index](../README.md)
- [`docs/[CXD] Specification.md`](../../../docs/[CXD]%20Specification.md)
