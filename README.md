# **CXD — Component Extensible Data**  
A compact, human‑friendly data format for configuration, manifests, and structured documents. CXD preserves the **JSON data model** while offering a dramatically more expressive surface syntax, making it ideal for tools, applications, and systems that need readable, maintainable configuration files.

---

## CXD in a Nutshell  
CXD is **JSON with ergonomics and extensibility**. It removes friction (quotes, braces, escaping) and adds powerful features for real‑world configuration work.

- **Unquoted keys**  
- **Implicit top‑level objects**  
- **Triple‑quoted multi‑line strings** with automatic indent stripping  
- **Line capture** for embedding raw DSLs or domain‑specific text  
- **Anchors & spread** for reusable configuration blocks  
- **Described lists** for compact tabular arrays  
- **Optional `@type:` header** for schema routing  
- **Fully JSON‑compatible output**

---

## The CXD Library  
The official parser and runtime for `.cxd` documents.

- **C implementation (`cxd_c`)** — small, embeddable, predictable  
- **C++ wrapper (`cxd`)** — RAII, exceptions, STL containers  
- **JSON exporter** — convert any CXD tree to JSON  
- **Stable value model** — objects, arrays, strings, numbers, booleans, null  
- **Full CXD feature support** — implicit objects, anchors, spread, described lists, line capture, numeric formats, strings, comments  
- **Type header extraction** — exposed as `doc.type`

The library is built for integration into CLIs, compilers, build systems, configuration‑driven applications, and domain‑specific tools.

---

## CXD Examples

### Basic configuration
```
@type:app.config

host: "localhost",
port: 8080,
features: [logging, metrics],
```

### Multi‑line strings
```
description: """
  A lightweight HTTP service.
  Supports OAuth2 and RBAC.
""",
```

### Line capture (raw DSL text)
```
query:     SELECT id, name FROM users WHERE active = 1,
layout:    @col ~240 =f5f5f5 +16 *0,
```

### Anchors & spread
```
&base = { timeout: 30, retries: 3 },

prod: { ...base, host: "api.example.com" },
dev:  { ...base, host: localhost, retries: 1 },
```

### Described lists
```
users: @[name, age] [
  ["Alice", 31],
  ["Bob",   24],
],
```

---

## CXD Design

CXD addresses the limitations of JSON in real‑world configuration -- Nope, I just wanted something else.

- **Human ergonomics** — fewer quotes, fewer braces, real comments  
- **Domain‑specific extensibility** — embed DSLs without escaping  
- **Reusable structure** — anchors and spread reduce duplication  
- **Schema routing** — `@type:` lets tools know what they’re parsing  
- **Cleaner multi‑line text** — indentation‑aware triple‑quoted strings  

It was built for configuration, manifests, UI layout documents, design tokens, and embedded data blocks inside larger syntaxes.

---

## Uses for CXD  
CXD is ideal for:

- Application configuration  
- Build and deployment manifests  
- UI layout and design token files  
- Test fixtures and structured data blocks  
- Tools that embed JSON‑like trees but need more expressive syntax  

It keeps the simplicity of JSON while giving you the expressive config files.

