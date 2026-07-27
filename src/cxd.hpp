#pragma once
// ─── CXD Parser Library ───────────────────────────────────────────────────────
// Component Extensible Data — v1.1
// Public API. Link against src/cxd.cpp or use via CMake target `cxd`.

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string_view>

namespace cxd {

// ─── Value ───────────────────────────────────────────────────────────────────

enum class Kind { Null, Bool, Number, String, Array, Object };

struct Value {
    Kind kind = Kind::Null;

    bool   boolean = false;   // Kind::Bool
    double number  = 0.0;     // Kind::Number
    std::string string;       // Kind::String (also line captures)
    std::vector<Value> array; // Kind::Array
    std::vector<std::pair<std::string, Value>> object; // Kind::Object, insertion-ordered

    // true when this String was produced by line capture (spec §4.4)
    bool isLineCapture = false;

    // Source location (1-based)
    int line = 0, col = 0;

    bool isNull()   const { return kind == Kind::Null;   }
    bool isBool()   const { return kind == Kind::Bool;   }
    bool isNumber() const { return kind == Kind::Number; }
    bool isString() const { return kind == Kind::String; }
    bool isArray()  const { return kind == Kind::Array;  }
    bool isObject() const { return kind == Kind::Object; }

    const Value* get(const std::string& key) const;
    Value*       get(const std::string& key);
    bool         has(const std::string& key) const;
    void         set(const std::string& key, Value v); // upsert

    static Value null_val()                            { return Value{}; }
    static Value bool_val(bool b)                      { Value v; v.kind=Kind::Bool;   v.boolean=b;            return v; }
    static Value num_val(double n)                     { Value v; v.kind=Kind::Number; v.number=n;             return v; }
    static Value str_val(std::string s, bool lc=false) { Value v; v.kind=Kind::String; v.string=std::move(s); v.isLineCapture=lc; return v; }
    static Value arr_val()                             { Value v; v.kind=Kind::Array;  return v; }
    static Value obj_val()                             { Value v; v.kind=Kind::Object; return v; }
};

// ─── Document ─────────────────────────────────────────────────────────────────

struct Document {
    std::optional<std::string> type; // from @type: header (nullopt if absent)
    Value root;
};

// ─── Errors ───────────────────────────────────────────────────────────────────

struct ParseError : std::runtime_error {
    int line, col;
    ParseError(const std::string& msg, int ln, int co)
        : std::runtime_error(msg), line(ln), col(co) {}
};

// ─── Key modes (§5.7) ────────────────────────────────────────────────────────

enum class KeyMode {
    Standard,    // [a-zA-Z_$][a-zA-Z0-9_$-~%!]*
    ECMAScript,  // [a-zA-Z_$][a-zA-Z0-9_$]*  (no hyphens)
};

// Returns the number of source bytes consumed by a valid key, or 0 on failure.
using KeyValidator = std::function<size_t(std::string_view)>;

// ─── Parse options ───────────────────────────────────────────────────────────

struct ParseOptions {
    // Recognise the @type: header (§3.1). Default: true.
    // Set to false for embedded CXD (e.g. Simplex inline `data { }` blocks)
    // where @type: is not a valid construct.
    bool allowTypeHeader = true;

    // Bare key recognition mode (§5.7). Default: Standard.
    KeyMode keyMode = KeyMode::Standard;

    // Optional host-provided key validator (§5.7). When set, this overrides
    // keyMode for bare keys while preserving quoted-key behavior.
    KeyValidator keyValidator;

    // Allow @-prefixed property keys (§5.7.2). Default: false.
    // When true, @key is parsed as a property key distinct from child keys.
    bool allowPropertyKeys = false;
};

// --- Public API -----------------------------------------------------------

Document    parse(const std::string& source,
                  const std::string& filename = "<input>",
                  ParseOptions opts = {});
Document    parseFile(const std::string& path, ParseOptions opts = {});
std::string toJson(const Value& v, int indent = 2);

} // namespace cxd
