# Component Extensible Data (CXD) Specification

| **Version**         | 0.99.001    |
| **File Extension:** | .cxd        |
| **CXD Signature:**  | none        |
| **Status:**         | Preliminary |

---

## 1. Introduction

**CXD** (Component Extensible Data) Format is a human-readable data serialization format. It provides several ergonomic features for config files and data documents:

- **No Top-Level Object** — The document itself is the outer `{}` and may be omitted
- **Unquoted Keys** — bare strings for simple identifier keys
- **Multi-line Strings** — Triple-quoted multi-line strings with automatic indent stripping
- **List Descriptors** — Compact homogenous array notation via list descriptors `@[…]`
- **Data Templates** — Named anchors and object spread for value reuse
- **Line Capture** — unquoted value lines are captured verbatim as text, enabling domain-specific applications to embed their own notation inside CXD without quoting or escaping

CXD's parsed output is a JSON-compatible data model (objects, arrays, strings, numbers, booleans, null). All CXD syntax is transformational sugar — no new types are introduced. Every valid JSON file with `"` strings is valid CXD.

---

## 2. Notation in this document

Grammar rules are written in EBNF. Terminals are `"quoted"`. Alternatives are separated by `|`. Optional elements appear in `[brackets]`. One-or-more repetitions use `+`, zero-or-more use `*`. Character ranges use `[a–z]` notation.

---

## 3. File Structure

A CXD file is UTF-8 encoded and contains exactly one **value** (§5). If the file begins in **implicit object mode** (§6.1), the outer `{}` of the top-level object is inferred.

### 3.1 Type Declaration Header

A CXD file MAY begin with an optional **type declaration** on the first non-blank, non-comment line. The type declaration identifies the domain-specific schema or application of this CXD document, giving parsers and tools an immediate routing signal before any data is read.

**Syntax:**

```
@type:NAMESPACE[.SUBTYPE]*
```

The `@type:` prefix is followed immediately (no space) by a dot-separated type identifier. The declaration occupies exactly one line and is terminated by a newline. No other content may appear on the same line.

```
@type:archetype

manifestName: acmeCoreEngine,
engineVersion: 4.2.001,
```

```
@type:layout

root:      column fill background=#f0f4f8,
sidebar:   column width=240 background=#1a1a2e padding=16,
```

```
@type:config

# Generic application configuration
host:    "localhost",
port:    8080,
```

**Rules:**

- The type declaration MUST appear before any `key: value` statements, anchor declarations (`&`), or array/object expressions
- Blank lines and `#` comments are permitted before the type declaration
- At most one `@type:` declaration is permitted per document
- The type identifier uses dot `.` as a namespace separator
- The `@type:` prefix is case-sensitive; `@TYPE:` and `@Type:` are not valid
- A document without a type declaration is a generic CXD document — valid and complete

**Disambiguation with described lists.** Document declarations begin with `@type:` — a bare name directly after `@`. Described lists begin with `@[` — a bracket directly after `@`. The two forms are unambiguous at the first character after `@`:

| First chars after `@`                      | Form                           |
| ------------------------------------------ | ------------------------------ |
| `[`                                        | Described list (`@[field, …]`) |
| `type:` (or any bare name followed by `:`) | Type declaration               |

**Pass 1 processing.** The CXD engine reads and strips document declarations before processing data statements. The type declaration is exposed as document metadata (`document.type`) and the key mode declaration as `document.keys` in the output of Pass 1 alongside the resolved data tree. They do not appear as keys in the data tree itself.

**Standard type identifiers:**

| Identifier        | Domain                                                            |
| ----------------- | ----------------------------------------------------------------- |
| `@type:data`      | Generic CXD data document ( the default when no type is declared) |
| `@type:archetype` | Archetype automation manifest                                     |
| `@type:layout`    | Host-defined layout document                                      |
| `@type:settings`  | Generic application configuration                                 |
| `@type:style`     | Design token file (colours, spacing, typography)                  |

Third-party and application-defined type identifiers are permitted. Implementations MUST NOT reject documents with unknown type identifiers; they SHOULD expose the identifier for host application use.

---

## 4. Lexical structure

### 4.1 Whitespace

Outside of string literals and numeric literals, the following characters are whitespace and are ignored:

| Character       | Code point |
| --------------- | ---------- |
| Space           | U+0020     |
| Horizontal tab  | U+0009     |
| Carriage return | U+000D     |
| Line feed       | U+000A     |

### 4.2 Comments

A `#` character outside of a string literal begins a **line comment**. Everything from `#` to the end of the line is ignored by the parser.

```
# This is a comment
name: "Alice",  # This is also a comment
```

### 4.3 Separators

Items within an **object** or **array** are separated by commas (`,`). A trailing comma after the last item is permitted. Newlines are whitespace; they do not separate items.

```
# Array — commas required between items, trailing comma allowed
tags: ["stable", "v3", "production"],

# Object — same rule
server: {
  host: "0.0.0.0",
  port: 8080,     # trailing comma
},
```

---

### 4.4 Line Capture

**Line capture** is a value form that captures an entire unquoted line verbatim as a plain text string. It provides CXD's core extensibility mechanism: when a value does not conform to any standard CXD type, it is captured as-is and handed to the host application for interpretation. This allows domain-specific applications to embed their own notation — layout directives, query expressions, DSL tokens, shader parameters, or any other structured text — directly inside a CXD document without quoting, escaping, or extending the CXD grammar.

**Trigger condition.** After a key–colon pair (`key:`), if the value does not match any of the standard value forms (object, array, described list, quoted string, number, typed literal, or bare string), the parser enters **line capture mode**.

Formally, line capture is triggered when the value after `:` is not initiated by any of:
- `{` — object
- `[` — array
- `@[` — described list (note: `@` alone without `[` triggers line capture)
- `"` or `'` — quoted string
- a digit or `-` followed by a digit — number
- `true` / `false` / `null` — typed literal
- a letter or `_` or `$` followed only by `[a-zA-Z0-9_$-]` — bare string

In line capture mode, all visible characters — letters, digits, symbols, spaces, and inline colons — are consumed verbatim until a terminating delimiter:

- `,` — item separator (not part of the captured value)
- `\n` or `\r\n` — line end (not part of the captured value)

The captured text is stored as an ordinary `text` string. The CXD engine performs no further parsing or interpretation of the captured content; all semantic meaning is the responsibility of the consuming host application.

```
# Standard CXD values — not line captures
color:    "#FF0000"                            # quoted string
count:    42                                   # number
enabled:  true                                 # boolean
routes:   @[method, path] [["GET", "/users"]]  # described list

# Line captures — verbatim text for any domain the host application understands
sidebar:   @col ~240 =f5f5f5 |r0 +16 *0 ^1,
query:     SELECT id, name FROM users WHERE active = 1,
gradient:  linear 45deg #0070f3 0% #7928ca 100%,
matrix:    1.0 0.0 0.0 | 0.0 1.0 0.0 | 0.0 0.0 1.0,
easing:    cubic-bezier(0.4, 0.0, 0.2, 1.0)
```

**Disambiguation summary.** The lexer resolves values in priority order:

| First character(s) after `:`                    | Value type       |
| ----------------------------------------------- | ---------------- |
| `{`                                             | Object           |
| `[`                                             | Array            |
| `@[`                                            | Described list   |
| `"` or `'`                                      | Quoted string    |
| digit or `-digit`                               | Number           |
| `true` / `false` / `null`                       | Typed literal    |
| `[a-zA-Z_$]` + only `[a-zA-Z0-9_$-]` characters | Bare string      |
| Anything else                                   | **Line capture** |

**Escaping.** There is no escape mechanism inside a line capture. If a value must contain a literal `,` or newline, use a quoted string instead.

**Inline colons.** A `:` inside a line capture (e.g. `+16:32`, `WHERE active = 1`) is part of the captured text and does NOT create a nested key–value pair. The CXD lexer only treats `:` as a key–value separator at the outermost statement level.

**Host application contract.** A host application consuming a CXD document that contains line captures MUST:
1. Accept line-captured values as plain `text` strings
2. Apply its own domain-specific parser to the text (the CXD engine never does this)
3. Not assume any particular grammar for line-captured content — different keys may carry different domain notations in the same document

Between the two approaches, the **Dual-Buffer Design (Line Capture Buffer + Shadow Token Buffer)** is definitively the better choice for your specification.

While the tracking-token approach requires you to specify complex backtracking rules or maintain an ambiguous predictive lexer, the dual-buffer design handles the implicit end-of-line fallback seamlessly. It allows the parser to always move forward, character by character, without needing to know the "true" data type until the line naturally concludes. This keeps the grammar clean for the author and mathematically deterministic for the compiler writer.

Here is the updated specification section, written in the same formal tone as your document, ready to be integrated into **`CXD_Specification_v1.1.000.md`**.

---

## 4.5 Line Capture Implementation Mechanics (Dual-Buffer Strategy)

To implement the implicit end-of-line (EOL) fallback without requiring destructive AST edits or cursor backtracking, conforming parsers SHOULD employ a **Dual-Buffer Evaluation Strategy** when processing statements.

```
                         ┌─────────────────────────────┐
                         │      [key:] Encountered     │
                         └─────────────────────────────┘
                                        │
                                        ▼
                         ┌─────────────────────────────┐
                         │  Parallel Buffer Stream     │
                         │  - Line Capture Buffer      │
                         │  - Shadow Token Buffer      │
                         └─────────────────────────────┘
                                        │
             ┌──────────────────────────┴──────────────┐
             ▼                                         ▼
  [End-of-Line / Delimiter]                 [Grammar Failure/Syntax Error]
             │                                         │
             ▼                                         ▼
             Is Shadow Buffer Valid?                   │
             ├── YES: Commit Shadow AST to Main Tree   │
             └── NO:  ─────────────────────────────────┘
                      │
                      ▼
             [Implicit Fallback Triggered]
             - Flush/Discard Shadow Buffer
             - Commit Line Capture Buffer as `text`

```

### 4.5.1 Buffer Definitions

Upon crossing the key-value structural separator (`:`), the parser instantiates two isolated, parallel data streams for the remainder of the statement value:

1. **Line Capture Buffer:** A raw string or character array that accumulates every incoming character verbatim—including leading/trailing whitespace, inline punctuation, structural brackets, and commas.
2. **Shadow Token Buffer:** A speculative scratchpad handled by a sub-lexer attempting to construct standard, strongly-typed JSON-compatible nodes (e.g., Objects, Arrays, Described Lists, Numbers, Booleans, or Null).

### 4.5.2 Evaluation and Commit Lifecycle

The dual buffers ingest tokens in parallel until a statement delimiter (such as an unescaped EOL or an outermost structural item separator) is reached. At the evaluation point, the parser executes the following routing logic:

* **Condition 1: Valid Shadow Stream.** If the content of the Shadow Token Buffer successfully conforms to a standard CXD data type structure at the boundary point, the shadow AST nodes are committed to the main document tree. The Line Capture Buffer is discarded.
* **Condition 2: Invalid Shadow Stream (Implicit Fallback).** If the sub-lexer encounters a structural or grammatical violation in the Shadow Token Buffer (e.g., a fragmented number like `1.0.0-beta` or an unquoted domain string like `SELECT`), the Shadow Token Buffer is instantly flagged as *invalid*. The parser continues reading the stream to the EOL delimiter, **flushes** the shadow scratchpad entirely, and commits the trimmed contents of the Line Capture Buffer into the main tree as a plain `text` value.

### 4.5.3 Deactivation of Structural Delimiters

Once the Shadow Token Buffer is flagged as invalid, the statement is locked into **Line Capture Mode**. For the remainder of that line, all standard structural characters lose their parsing power:

* **Commas (`,`)** cease to act as item separators and are absorbed as verbatim text unless followed immediately by a valid `next-key:` sequence at the outermost object level.
* **Comments (`#`)** contained within a locked Line Capture Buffer are treated as verbatim string text unless explicitly preceded by whitespace, preserving inline hex tokens (e.g., `#FF0000`) and URLs.


### 4.5.4 Comment Resolution and Trimming

While in **Line Capture Mode** (Condition 2), the `#` character is structurally evaluated by inspecting the tail of the preceding text slice before the buffer is committed:

1. **Standalone/Trailing Comments:** If a `#` is encountered and the immediately preceding non-whitespace character is a structural comma (`,`), or if the `#` is preceded by explicit whitespace following a completed token, it is treated as a valid comment.
* **Parsing Action:** The parser stops reading into both buffers for that statement. The characters from the `#` to the end of the line are treated as a comment, and any trailing comma preceding the comment is handled as a structural item separator for the parent object.
* *Example:* `tags: frontend, backend, # deployment flags` -> Captures a structured array or splits at the comma, ignoring the comment.


2. **Inline Literal Comments:** If a `#` is encountered *without* a preceding comma or isolating whitespace (e.g., embedded directly inside a continuous string of non-whitespace characters), it loses its comment status.
* **Parsing Action:** The `#` and all subsequent characters on the line are consumed verbatim by the Line Capture Buffer.
* *Example:* `color: #FF0000` -> The `#` does not follow a comma or whitespace separating a token; the entire string `#FF0000` is captured safely as a single literal text block.

---

### Updated Architectural Decision Flow

Integrating this rule ensures that the lexer remains forward-only, evaluating the single character preceding the `#` to determine whether to slice the buffer or keep streaming:

```
                       Encounter '#' Character
                                  │
         ┌────────────────────────┴────────────────────────┐
         ▼                                                 ▼
Preceded by ',' OR separating space?             Embedded within token string?
         │                                                 │
         ▼                                                 ▼
[Treat as Comment]                               [Treat as Verbatim Text]
- Stop buffer accumulation.                      - Append '#' to Line Capture Buffer.
- Strip comment to EOL.                          - Continue streaming line to EOL.
- Keep structural comma.

```

This ensures that developers can still safely write standard inline comments at the end of structured blocks or line items without losing the ability to embed raw hex codes, URL hashes, or domain notations that rely heavily on the `#` symbol.
---

## 5. Values

### 5.1 Null

```
null
```

The literal `null` represents the absent value.

### 5.2 Booleans

```
true
false
```

### 5.3 Numbers

#### 5.3.1 Decimal integers and floats

```
42
-7
3.14159
-0.001
6.022e23
1.5e-10
```

#### 5.3.2 Space as a digit-grouping separator

Within a decimal numeric literal, a **space** (U+0020) between two consecutive digit characters is treated as a visual separator and is ignored by the parser. This applies in both the integer and fractional parts of a number. It does **not** apply adjacent to a decimal point (`.`), exponent marker (`e`/`E`), sign (`+`/`-`), or within extended literals.

```
1 000 000          # → 1000000
10 485 760         # → 10485760
3.141 592 653      # → 3.141592653
9 007 199 254 740 991  # → 9007199254740991
```

Multiple consecutive spaces between digit groups are permitted and are all ignored.

The space separator ends where a non-digit non-space character is encountered. Because commas are item separators (§4.3), `1 000, 2` unambiguously denotes the number `1000` followed by the number `2`.

#### 5.3.3 Extended literals

Extended integer literals use a radix prefix. The underscore `_` character may be used as a visual separator within extended literals. Space grouping (§5.3.2) does **not** apply to extended literals.

| Prefix | Radix | Digits            | Example               |
| ------ | ----- | ----------------- | --------------------- |
| `0x`   | 16    | `[0–9 a–f A–F _]` | `0xFF`, `0xDEAD_BEEF` |
| `0b`   | 2     | `[0 1 _]`         | `0b1010_1100`         |
| `0o`   | 8     | `[0–7 _]`         | `0o755`               |

All numeric values — decimal, hex, binary, and octal — are serialized to standard JSON numbers.

### 5.4 Strings

#### 5.4.1 Quoted strings

Single-line strings delimited by `"` (double quote) or `'` (single quote). The following escape sequences are recognized:

| Escape   | Meaning            |
| -------- | ------------------ |
| `\\`     | Backslash          |
| `\"`     | Double quote       |
| `\'`     | Single quote       |
| `\n`     | Line feed          |
| `\r`     | Carriage return    |
| `\t`     | Horizontal tab     |
| `\uXXXX` | Unicode code point |

```
"Hello, world!"
'It\'s a single-quoted string'
"Line 1\nLine 2"
"\u2603 snowman"
```

#### 5.4.2 Triple-quoted strings

Multi-line strings delimited by `"""`. Content begins on the line immediately after the opening `"""` and ends before the closing `"""` (which must appear on its own line, optionally preceded by whitespace).

**Automatic indent stripping:** the common leading whitespace of all non-empty content lines is measured and removed. This allows the string body to be indented freely to match the surrounding document structure.

```
description: """
  A lightweight HTTP service.
  Supports OAuth2 and RBAC.
""",

query: """
  SELECT id, email
  FROM users
  WHERE active = true
  ORDER BY created DESC
""",
```

The first example parses to `"A lightweight HTTP service.\nSupports OAuth2 and RBAC."`.

Escape sequences are **not** interpreted inside triple-quoted strings. The sequence `"""` cannot appear within a triple-quoted string body.

#### 5.4.3 Bare strings

A value token beginning with a letter (`[a-zA-Z_$]`) and containing no whitespace, commas, or structural characters (`{ } [ ] : # @ & * .`) is a **bare string** and is interpreted as a plain string value without quotation marks.

The reserved words `true`, `false`, and `null` are **never** bare strings; they produce their typed values.

```
env:    production,
region: us-east-1,
status: active,
method: GET,
```

Bare strings are intended for simple identifiers. Values containing spaces, `#`, or structural characters require quoted strings.

### 5.5 Arrays

An ordered list of zero or more values enclosed in `[` and `]`, with items separated by commas. A trailing comma is permitted.

```
[1, 2, 3]
["alpha", "beta", "gamma"]
[true, false, null]

# Multiline — comma goes at the end of each line
[
  "item one",
  "item two",
  "item three",
]
```

### 5.6 Described lists

A compact notation for arrays whose items are all objects sharing the same shape. A **list descriptor** `@[field1, field2, …]` precedes an array of positional inner arrays. Each inner array is zipped with the field names to produce an object.

```
users: @[name, age, role] [
  ["Alice", 31, "admin"],
  ["Bob",   24, "viewer"],
  ["Carol", 28, "editor"],
],
```

This is exactly equivalent to:

```json
[
  {"name": "Alice", "age": 31, "role": "admin"},
  {"name": "Bob",   "age": 24, "role": "viewer"},
  {"name": "Carol", "age": 28, "role": "editor"}
]
```

If an inner array has fewer values than the descriptor has fields, the missing values are `null`. An inner array may not have more values than the descriptor has fields.

### 5.7 Objects

An unordered set of key-value pairs enclosed in `{` and `}`, pairs separated by commas. A trailing comma is permitted.

```
{
  host: "localhost",
  port: 8080,
  tls: true,
}
```

#### 5.7.1 Keys

CXD key recognition is controlled by the parser's active **key validator**. A key validator is a developer-facing parser configuration option. It is not declared inside the CXD document and does not introduce any document-level syntax.

The default CXD key validator is the **standard key validator**:

```ebnf
standard-key = [a-zA-Z_$] [a-zA-Z0-9_$-]*
```

Keys accepted by the active validator may be written **bare** (without quotation marks). Under the standard key validator, hyphens are valid within a bare key but not as the first character. Quoted keys (§5.4.1) accept any string and remain available for keys outside the active validator's accepted form.

```
{
  host: "localhost",            # accepted by the standard validator
  max-retries: 3,               # accepted by the standard validator
  "Content-Type": "text/html", # quoted key — accepted regardless of validator
}
```

Host applications MAY supply an alternate key validator when parsing a document. The validator determines which unquoted key forms are accepted at key positions. This mechanism is intended for domain-specific applications that need richer key syntax while preserving the CXD data model.

For example, a layout-oriented host application MAY use a selector key
validator that accepts state and breakpoint suffixes:

```
button:              box width=120 height=40 color=#0070f3,
button&hover:        color=#005acc,
sidebar@sm:          column width=full gap=56,
button&disabled@md:  opacity=50%,
```

A selector key validator may reject keys that are legal under the standard
validator but invalid as selector keys:

```
button-hover:  box width=120 height=40,  # rejected by selector validator
.content:      column width=full,        # rejected by selector validator
button hover:  box width=120 height=40,  # rejected by selector validator
```

The active key validator affects only key recognition and validation for object keys and described-list descriptor fields. It MUST NOT change CXD's value model, object semantics, arrays, anchors, spreads, comments, line capture, type declaration behavior, or serialization output.

##### Built-in validator profiles

Conforming implementations MUST provide the standard key validator. Implementations MAY provide additional named validator profiles as parser options. Suggested core profiles include:

| Profile      | Purpose                     | Accepted examples            | Rejected examples       |
| ------------ | --------------------------- | ---------------------------- | ----------------------- |
| `standard`   | Default CXD compatibility   | `host`, `max-retries`, `_id` | `.class`, `two words`   |
| `ecmascript` | JavaScript-like identifiers | `host`, `$root`, `_id`       | `max-retries`, `.class` |

Domain-specific validators, such as selector keys, SHOULD be supplied by the
host application or a domain library rather than required by CXD core.

Validator profile selection is outside the CXD source text. It is supplied by the parser caller, host application, command-line option, API parameter, or implementation configuration. A CXD document MUST NOT declare its key validator using document syntax.


#### 5.7.2 Property keys

When the host application enables **property keys**, object keys may be prefixed with `@` to distinguish **node properties** from **child entries**. This provides a clean separation between metadata that describes a node and structural children that belong beneath it — analogous to the attribute/element distinction in XML.

```
panel:
    @id: "main-nav"
    @class: "sidebar"
    @visible: true
    logo:
        @src: "logo.svg"
        @size: 16
    links:
        home: "/"
        about: "/about"
```

In the parsed output, the `@` prefix is preserved in the key string. Consumers distinguish properties from children by checking whether a key starts with `@`. The CXD value model is unchanged — no new node types or data structures are introduced.

##### Rules

1. A property key is `@` followed immediately by a key that is valid under the active key validator. No whitespace between `@` and the key name.
2. Property keys are valid anywhere a regular key is valid: explicit objects, implicit top-level objects, and described-list field descriptors.
3. The `@` prefix is **not** valid on anchor names, spread targets, or anchor references — it applies only to object keys.
4. Quoted property keys are permitted: `"@id"` is equivalent to `@id`.
5. Property key support is a parser option, not a document declaration. It is enabled by the host application. When disabled (the default), `@` at key position is a parse error.
6. The `@type:` document declaration is not affected — it is parsed before any key position is reached and remains a document-level construct.

##### Disambiguation

| Position                    | `@` followed by | Interpretation                |
| --------------------------- | --------------- | ----------------------------- |
| Document start, column 1    | letter          | `@type:` document declaration |
| Document start, column 1    | `[`             | `@[…]` described list         |
| Key position in object body | letter          | Property key (when enabled)   |

#### 5.7.3 Anchor declarations and spread

See §6.

---

## 6. Document-level features

### 6.1 Implicit top-level object

If the first meaningful token of a CXD file is one of:

- A validator-accepted bare key or quoted string followed by `:`
- An anchor declaration (`&`)
- A spread expression (`...`)

...the parser enters **implicit object mode**: the entire file body is treated as the content of a top-level `{ }` block, and the outer braces are not required. Statements at the top level are still separated by commas.

```
# config.cxd — implicit object mode
name: "Acme API",
version: 3,
env: production,
debug: false,
```

Equivalent to:

```
{
  name: "Acme API",
  version: 3,
  env: production,
  debug: false,
}
```

A CXD file whose first token is `[`, `"`, a number, `true`, `false`, or `null` is **not** in implicit object mode; it is an explicit value of that type.

### 6.2 Anchors

An **anchor** stores a value under a name for later reference. An anchor declaration is a statement (valid inside any object body or at the implicit top level). Anchors do **not** produce a key in the parsed output.

**Declaration syntax:** `&name = value` (the `=` sign is optional: `&name value` is equivalent).

**Reference syntax:** `*name` produces a deep clone of the anchored value wherever a value is expected.

```
&timeout-cfg = {connect: 5, read: 30, write: 10},

services: {
  auth:    {host: "auth.internal",    timeouts: *timeout-cfg},
  billing: {host: "billing.internal", timeouts: *timeout-cfg},
},
```

Anchor names follow the standard key validator. Quoted anchor names are not permitted.

An anchor must be declared before any reference to it. Referencing an undeclared anchor is an error.

### 6.3 Spread

The `...` operator, followed by an anchor name, a `*name` reference, or an object literal, **merges** the target object's key-value pairs into the enclosing object. Keys specified after a spread override keys introduced by it. Spreading a non-object value is an error.

The spread is a **statement** within an object body; it must be followed by a comma like any other statement (unless it is the last item).

```
&base = {
  timeout: 30,
  retries: 3,
  tls: true,
},

development: {
  ...base,
  host: localhost,
  tls: false,    # overrides tls from base
},

production: {
  ...base,
  host: "api.example.com",
},
```

The three spread forms are equivalent:

| Form           | Meaning                          |
| -------------- | -------------------------------- |
| `...base`      | Spread anchor named `base`       |
| `...*base`     | Same — explicit reference syntax |
| `...{k: v, …}` | Spread an inline object literal  |

---

## 7. Formal grammar

```
document       = [type-decl] (implicit-obj | value)

# ── Document declarations (§3.1, §5.7.4) ───────────────────────────────────
type-decl      = "@type:" type-id NEWLINE
type-id        = type-name ("." type-name)*
type-name      = [a-zA-Z_$] [a-zA-Z0-9_$-]*
                 # must appear before any data statement; at most one per document

implicit-obj   = stmt ("," stmt)* [","]

value        = object
             | array
             | described
             | triple-str
             | string
             | number
             | "true" | "false" | "null"
             | bare-str
             | line-capture         # §4.4 — verbatim line text for domain-specific use
             | anchor-ref

# ── Objects ────────────────────────────────────────────────
object       = "{" [body] "}"
body         = stmt ("," stmt)* [","]

stmt         = anchor-decl
             | spread
             | key ":" stmt-value

# stmt-value uses priority routing — line-capture is the fallback
# after all other value forms are tried in the order listed in value above
stmt-value   = value

anchor-decl  = "&" name ["="] value
anchor-ref   = "*" name
spread       = "..." (name | "*" name | object)

key          = mode-key | string

# ── Arrays ─────────────────────────────────────────────────
array        = "[" [value ("," value)* [","]] "]"
described    = "@[" key ("," key)* [","] "]" array
               # described is only triggered by exactly "@["
               # "@" not followed by "[" falls through to line-capture

# ── Tokens ─────────────────────────────────────────────────
mode-key     = token accepted by the active key validator (§5.7.1)
bare-str     = [a-zA-Z_$] [^ \t\n\r,{}[\]:@&*.#]*

# Line capture — fallback value form (§4.4)
# Triggered when no other value form matches at a stmt-value position.
# Captures all visible characters to the next comma or newline.
# Only valid as a stmt-value (after key:); not valid as an array element
# or standalone value outside a key:value context.
line-capture = [^ \t][^,\n\r]*
               # leading whitespace already consumed before value dispatch;
               # result stored as a plain text string

name         = mode-key

string       = '"' dq-char* '"'
             | "'" sq-char* "'"

triple-str   = '"""' newline line* ws '"""'
             # common leading whitespace stripped from all content lines

# ── Numbers ─────────────────────────────────────────────────
number       = ["-"] ( hex | binary | octal | decimal )

decimal      = int-part ["." frac-part] [exp]
int-part     = digit (sp* digit)*   # sp: space used as grouping separator
frac-part    = digit (sp* digit)*
exp          = ("e" | "E") ["+" | "-"] digit+
               # spaces are NOT permitted within the exponent

hex          = "0x" [0-9a-fA-F_]+
binary       = "0b" [01_]+
octal        = "0o" [0-7_]+
               # _ is the grouping separator for extended literals
               # space grouping does NOT apply to extended literals

digit        = [0-9]
sp           = U+0020

# ── Structure ────────────────────────────────────────────────
comment      = "#" (any char except newline)* newline
separator    = ","
whitespace   = U+0020 | U+0009 | U+000D | U+000A
```

---

## 8. Relationship to JSON

Every valid JSON document is valid CXD (JSON has no comment syntax, so no `#` conflicts exist). A CXD parser always produces a JSON-compatible data model; no new value types are introduced.

**CXD → JSON conversion** is always lossless: parse CXD, emit JSON.

**JSON → CXD conversion** is heuristic but reversible:

| JSON pattern             | CXD representation                  |
| ------------------------ | ----------------------------------- |
| Top-level object         | Implicit object (omit outer `{}`)   |
| Array of uniform objects | Described list `@[…] […]`           |
| String containing `\n`   | Triple-quoted string                |
| Integer ≥ 10 000         | Space-grouped decimal (`1 000 000`) |
| Any number               | Plain decimal                       |

---

## 9. Examples

### 9.1 Application configuration

```
# config.cxd
name: "Acme API",
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

tags: ["stable", "v3", "production"],
```

### 9.2 Multi-environment config with anchors

```
# Shared base — not a key in output
&base = {
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
},
```

### 9.3 Extended numbers and described lists

```
# Bitflags
permissions: {
  read:    0b0001,
  write:   0b0010,
  execute: 0b0100,
  admin:   0xFF,
  default: 0o644,
},

# Space-grouped decimals
limits: {
  max-connections: 1 000 000,
  max-payload:     10 485 760,
  rate-limit:      1 000,
  pi:              3.141 592 653 589 793,
},

# Compact table — 8 rows, field names declared once
benchmarks: @[name, ops-sec, p50-ms, p99-ms, pass] [
  ["read-cached",  2 500 000, 0.04, 0.12, true],
  ["read-db",         85 000, 1.20, 8.50, true],
  ["write-db",        42 000, 2.40, 15.0, true],
  ["write-indexed",   38 000, 2.60, 18.0, false],
],
```

### 9.4 Multi-line strings

```
name: "my-service",
version: "2.0.0",
license: MIT,

description: """
  A lightweight HTTP service for user management.
  Supports OAuth2, RBAC, and multi-tenancy out of the box.
""",

startup-sql: """
  CREATE TABLE IF NOT EXISTS users (
    id      SERIAL PRIMARY KEY,
    email   TEXT NOT NULL UNIQUE,
    name    TEXT,
    active  BOOLEAN DEFAULT true,
    created TIMESTAMPTZ DEFAULT now()
  )
""",
```

### 9.5 Nested anchors and deep config

```
&db-defaults = {
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
    pool-size: 20,    # override — replicas need larger pool
  },
  analytics: {
    ...db-defaults,
    host: "db-analytics.internal",
    name: "acme_analytics",
    port: 5433,
    ssl: false,
  },
},
```

---

## 10. Error conditions

A conforming parser must reject the following:

| Condition                                  | Example                |
| ------------------------------------------ | ---------------------- |
| Unclosed string                            | `"hello`               |
| Unclosed object                            | `{key: val`            |
| Unclosed array                             | `[1, 2`                |
| Missing colon after key                    | `{name "Alice"}`       |
| Missing comma between items                | `{a: 1 b: 2}`          |
| Unknown anchor reference                   | `*undefined`           |
| Spread of non-object                       | `...42`                |
| Described list item wider than descriptor  | `@[a, b] [[1, 2, 3]]`  |
| Number with space adjacent to `.` or `e`   | `1 .5`, `1. 5`, `1e 3` |
| Extended literal with space separator      | `0x1 FF`               |
| Triple-quoted string with no closing `"""` | `"""hello`             |

A conforming parser **should** emit a diagnostic message identifying the location and nature of the error.

---

## 11. Design rationale

**`#` for comments** — `#` is the comment character in shell, Python, Ruby, YAML, TOML, and most INI formats. Users writing config files are already fluent in `#`. The `//` alternative is borrowed from C-family languages and sits awkwardly in non-code contexts.

**Commas as separators** — Commas are explicit, unambiguous, and visually distinct from newlines. Relying on newlines as implicit separators (as YAML does) introduces subtle bugs when lines are reordered, wrapped, or pasted. Commas make structure visible at a glance and are consistent with JSON, making mechanical conversion straightforward.

**Space as digit separator** — The underscore separator (`1_000_000`) requires the reader to know the convention; a space (`1 000 000`) is self-evident to anyone who reads numbers in any language. Because commas are required separators, `1 000` is unambiguous as a single number: if two numbers were intended they would be written `1, 000`. The space separator does not apply to extended literals (hex, binary, octal) where visual structure is already supplied by the prefix, and where spaces would create ambiguity with surrounding whitespace.

**Implicit top-level object** — Config files almost universally represent a single object. Requiring `{` and `}` to wrap the entire file adds two lines of pure noise. The detection heuristic (first token is `key:` or `&` or `...`) is mechanically unambiguous.

**Anchors over merge keys** — YAML's merge key (`<<: *anchor`) reuses an existing key name in an ad-hoc way. CXD's `&name` / `...name` separates declaration from expansion using dedicated syntax that scans clearly even to readers unfamiliar with the convention.

**Described lists** — JSON has no compact table notation. When a dataset has many rows and a fixed schema, repeating the key names on every row wastes space and hurts readability. `@[field, …]` declares the schema once; rows become positional arrays. The saving scales with row count and column width: a table with 8 columns and 100 rows saves roughly 60–70% of characters compared to equivalent JSON.

---

## 12. Domain-Specific Applications via Line Capture

CXD is designed to serve as the data layer for a wide range of domain-specific applications. Line capture (§4.4) is the mechanism that makes this possible: any value that the CXD engine does not recognise as a standard type is passed through verbatim as `text`. The host application, not the CXD engine, is responsible for giving that text meaning.

This separation is formalised as a **two-pass architecture**. The CXD engine executes Pass 1 (pure data structure); the domain-specific engine executes Pass 2 (domain semantics). CXD is agnostic to what happens in Pass 2 — it may be a layout system, a query language, a shader parameter format, a configuration DSL, or anything else.

```
┌───────────────────┐     Pass 1       ┌──────────────────────────────┐
│  Raw CXD Source   │ ───────────────► │  In-Memory Intermediate Rep  │
│      (.cxd)       │  (Data Engine)   │   (JSON-Compatible Tree)     │
└───────────────────┘                  └──────────────────────────────┘
                                                      │
                                                      │ Pass 2
                                                      ▼
                                       ┌──────────────────────────────┐
                                       │   Host Application Engine    │
                                       │   (Domain-Specific Evaluator)│
                                       └──────────────────────────────┘
```

### 12.1 Pass 1 — CXD Structural Processing

The CXD engine processes the source file completely blind to the semantic meaning of any line-captured values. It executes only structural data operations:

- Strip `#` line comments
- Build key–value relationships and nested object trees
- Resolve anchor declarations (`&name`) and apply spread expansions (`...name`)
- Evaluate described list headers `@[...]` into repeated record keys
- Capture all line-capture values as opaque plain `text` strings (§4.4)

The output is a fully resolved JSON-compatible in-memory object tree. Line-captured values appear as ordinary `text` strings — their internal syntax is completely invisible to the CXD engine. The same CXD document may contain line-captured values from multiple different domain notations simultaneously.

### 12.2 Pass 2 — Domain-Specific Evaluation

The host application receives the resolved object tree from Pass 1 and walks it. When it encounters a line-captured `text` value, it routes it to the appropriate domain evaluator — identified by key name, schema annotation, or application convention.

The domain evaluator may be any processor appropriate to the embedded notation:

| Domain           | Example notation                               | Evaluator           |
| ---------------- | ---------------------------------------------- | ------------------- |
| Layout hints     | `column width=240 padding=16`                  | Layout engine       |
| SQL query        | `SELECT id FROM users WHERE active = 1`        | SQL query engine    |
| Shader parameter | `linear 45deg #0070f3 0% #7928ca 100%`         | Shader compiler     |
| Transform matrix | `1.0 0.0 0.0 / 0.0 1.0 0.0 / 0.0 0.0 1.0`     | Math library        |
| CSS-like rules   | `bold 14px/1.4 system-ui,sans-serif`           | Style engine        |
| Custom DSL       | any domain-specific notation                   | Host-defined        |

**Architectural separation rule.** Isolating data structure (Pass 1) from domain semantics (Pass 2) means:
- The CXD engine never needs updating to support new domain notations
- Domain evaluators never need to parse CXD — they receive pre-flattened strings
- Multiple different domain notations can coexist in a single CXD document
- CXD anchors and spread expansions work on line-captured values just as on any other value

### 12.3 Schema Typing for Line-Captured Values

Line-captured values are plain `text` at the CXD level. They integrate transparently with schema validation — no special handling is needed. A schema simply declares line-capture fields as `text`, optionally using a named type alias to communicate intent to readers:

```
{
  "sidebar":   "text",
  "main":      "text",
  "userQuery": "text",
  "easing":    "text"
}
```

These schema annotations carry no semantic weight in the CXD engine. They exist
solely as documentation or as contracts enforced by the consuming application.
The evaluator for each field is selected by the host application, not by CXD.
