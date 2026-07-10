// CXD parser tests — run with: cmake -DCXD_BUILD_TESTS=ON && make cxd_tests
#include "cxd/cxd.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

#define PASS(name) std::printf("PASS  %s\n", name)
#define FAIL(name, msg) do { std::fprintf(stderr, "FAIL  %s: %s\n", name, msg); failures++; } while(0)

static int failures = 0;

// ── helpers ────────────────────────────────────────────────────────────────────

static cxd::Document p(const char* src) { return cxd::parse(src); }

static void check(bool cond, const char* test, const char* detail = "") {
    if (!cond) FAIL(test, detail);
    else       PASS(test);
}

// ── basic types ────────────────────────────────────────────────────────────────

static void testNull() {
    auto doc = p("null");
    check(doc.root.isNull(), "null value");
}

static void testBool() {
    check(p("true").root.boolean  == true,  "true");
    check(p("false").root.boolean == false, "false");
}

static void testNumbers() {
    check(p("42").root.number           == 42,       "integer");
    check(p("-7").root.number           == -7,       "negative");
    check(p("3.14").root.number         == 3.14,     "float");
    check(p("1 000 000").root.number    == 1000000,  "space grouping");
    check(p("3.141 592").root.number    == 3.141592, "space grouping frac");
    check(p("0xFF").root.number         == 255,      "hex");
    check(p("0b1010").root.number       == 10,       "binary");
    check(p("0o755").root.number        == 493,      "octal");
    check(p("0xDEAD_BEEF").root.number  == 3735928559.0, "hex with underscore");
    check(p("6.022e23").root.number     == 6.022e23, "scientific");
}

static void testStrings() {
    check(p("\"hello\"").root.string   == "hello",  "double-quoted");
    check(p("'world'").root.string     == "world",  "single-quoted");
    check(p("\"a\\nb\"").root.string   == "a\nb",   "escape \\n");
    check(p("\"\\u0041\"").root.string == "A",      "unicode escape");
}

static void testTripleString() {
    const char* src = R"("""
  Line one.
  Line two.
""")";
    auto v = p(src).root;
    check(v.string == "Line one.\nLine two.", "triple-quoted indent strip");
}

static void testBareString() {
    auto v = p("production").root;
    check(v.isString() && v.string == "production" && !v.isLineCapture, "bare string");
}

static void testArray() {
    auto v = p("[1, 2, 3]").root;
    check(v.isArray() && v.array.size() == 3 &&
          v.array[0].number == 1 && v.array[2].number == 3, "array");
}

static void testObject() {
    auto doc = p("{ host: \"localhost\", port: 8080, }");
    auto& v = doc.root;
    check(v.isObject(), "object kind");
    check(v.has("host") && v.get("host")->string == "localhost", "object field host");
    check(v.has("port") && v.get("port")->number == 8080,        "object field port");
}

// ── implicit object (§6.1) ───────────────────────────────────────────────────

static void testImplicit() {
    const char* src =
        "name: \"Acme\",\n"
        "version: 3,\n"
        "env: production,\n";
    auto doc = p(src);
    check(doc.root.isObject(),                                  "implicit object");
    check(doc.root.get("name")->string == "Acme",               "implicit name");
    check(doc.root.get("version")->number == 3,                 "implicit version");
    check(doc.root.get("env")->string == "production",          "implicit bare env");
}

// ── @type header (§3.1) ──────────────────────────────────────────────────────

static void testTypeHeader() {
    const char* src = "@type:simplex.config\n\nhost: localhost,\n";
    auto doc = p(src);
    check(doc.type.has_value() && *doc.type == "simplex.config", "@type header");
    check(doc.root.has("host"), "@type + implicit obj");
}

// ── anchors & spread (§6.2, §6.3) ───────────────────────────────────────────

static void testAnchors() {
    const char* src =
        "&base = { timeout: 30, tls: true },\n"
        "dev: { ...base, host: localhost, tls: false, },\n"
        "prod: { ...base, host: \"api.example.com\", },\n";
    auto doc = p(src);
    auto* dev  = doc.root.get("dev");
    auto* prod = doc.root.get("prod");
    check(dev  && dev->get("timeout")->number  == 30,    "spread timeout dev");
    check(dev  && dev->get("tls")->boolean     == false, "spread override tls");
    check(prod && prod->get("tls")->boolean    == true,  "spread tls prod");
    check(!doc.root.has("base"),                         "anchor not in output");
}

// ── described list (§5.6) ────────────────────────────────────────────────────

static void testDescribed() {
    const char* src =
        "users: @[name, age, role] [\n"
        "  [\"Alice\", 31, \"admin\"],\n"
        "  [\"Bob\",   24, \"viewer\"],\n"
        "],\n";
    auto doc = p(src);
    auto* users = doc.root.get("users");
    check(users && users->isArray() && users->array.size() == 2, "described list size");
    auto& alice = users->array[0];
    check(alice.get("name")->string == "Alice" &&
          alice.get("age")->number  == 31      &&
          alice.get("role")->string == "admin", "described list row Alice");
}

// ── line capture (§4.4) ──────────────────────────────────────────────────────

static void testLineCapture() {
    const char* src =
        "sidebar: @col ~240 =f5f5f5 |r0 +16 *0,\n"
        "query: SELECT id FROM users WHERE active = 1,\n";
    auto doc = p(src);
    auto* sidebar = doc.root.get("sidebar");
    auto* query   = doc.root.get("query");
    check(sidebar && sidebar->isLineCapture, "sidebar is line capture");
    check(sidebar->string == "@col ~240 =f5f5f5 |r0 +16 *0", "sidebar content");
    check(query && query->isLineCapture,     "query is line capture");
    check(query->string == "SELECT id FROM users WHERE active = 1", "query content");
}

// ── inline # in line capture ─────────────────────────────────────────────────

static void testHashInLineCapture() {
    // #FF0000 — '#' not preceded by whitespace → literal
    auto doc = p("color: #FF0000,\n");
    auto* c = doc.root.get("color");
    check(c && c->isLineCapture && c->string == "#FF0000", "hash hex in line capture");
}

// ── ParseOptions: allowTypeHeader=false (embedded CXD context) ──────────────

static void testEmbeddedNotype() {
    cxd::ParseOptions noType;
    noType.allowTypeHeader = false;
    // Normal document parses fine; doc.type is always empty
    auto doc = cxd::parse("name: Acme,\n", "<input>", noType);
    check(!doc.type.has_value(), "doc.type empty when allowTypeHeader=false");
    // @type: at the start is a syntax error in embedded context
    bool threw = false;
    try { cxd::parse("@type:archetype\nname: Acme,\n", "<input>", noType); }
    catch (const cxd::ParseError&) { threw = true; }
    check(threw, "@type: is ParseError when allowTypeHeader=false");
}

// ── toJson ────────────────────────────────────────────────────────────────────

static void testToJson() {
    auto doc = p("{ a: 1, b: true, c: null, d: \"hi\", }");
    std::string j = cxd::toJson(doc.root, 0);
    // Rough check — just verify it round-trips as valid-looking JSON
    check(j.find("\"a\":1")   != std::string::npos, "json num");
    check(j.find("\"b\":true")!= std::string::npos, "json bool");
    check(j.find("\"c\":null")!= std::string::npos, "json null");
    check(j.find("\"d\":\"hi\"")!=std::string::npos,"json str");
}

// ── error conditions ─────────────────────────────────────────────────────────

static void testErrors() {
    auto tryParse = [](const char* src) {
        try { cxd::parse(src); return false; } catch (const cxd::ParseError&) { return true; }
    };
    check(tryParse("{ key val }"),  "error: missing colon");
    check(tryParse("[1, 2"),        "error: unclosed array");
    check(tryParse("{ key: val"),   "error: unclosed object");
    check(tryParse("*undefined"),   "error: unknown anchor ref");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("─── cxd parser tests ───\n");
    testNull();
    testBool();
    testNumbers();
    testStrings();
    testTripleString();
    testBareString();
    testArray();
    testObject();
    testImplicit();
    testTypeHeader();
    testAnchors();
    testDescribed();
    testLineCapture();
    testHashInLineCapture();
    testEmbeddedNotype();
    testToJson();
    testErrors();
    std::printf("─── %s (%d failure%s) ───\n",
        failures ? "FAILED" : "OK",
        failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
