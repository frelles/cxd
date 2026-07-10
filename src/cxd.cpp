#include "cxd/cxd.hpp"
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>

namespace cxd {

// ─── Value helpers ────────────────────────────────────────────────────────────

const Value* Value::get(const std::string& key) const {
    for (auto& [k, v] : object) if (k == key) return &v;
    return nullptr;
}
Value* Value::get(const std::string& key) {
    for (auto& [k, v] : object) if (k == key) return &v;
    return nullptr;
}
bool Value::has(const std::string& key) const { return get(key) != nullptr; }
void Value::set(const std::string& key, Value v) {
    for (auto& [k, val] : object) if (k == key) { val = std::move(v); return; }
    object.push_back({key, std::move(v)});
}

// ─── Parser ───────────────────────────────────────────────────────────────────

class Parser {
public:
    Parser(const std::string& source, const std::string& filename, ParseOptions opts)
        : src_(source), filename_(filename), opts_(opts) {}

    Document parse() {
        Document doc;
        pos_ = 0; line_ = 1; col_ = 1;

        skipWS();
        tryTypeDecl(doc);
        skipWS();

        if (isImplicitObject()) {
            doc.root = Value::obj_val();
            parseBody(doc.root, /*implicit=*/true);
        } else {
            doc.root = parseValue(/*inStmt=*/false);
        }
        return doc;
    }

private:
    const std::string& src_;
    std::string filename_;
    ParseOptions opts_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;
    std::unordered_map<std::string, Value> anchors_;

    // ── Character primitives ─────────────────────────────────────────────────

    bool atEnd() const { return pos_ >= src_.size(); }

    char peek(size_t offset = 0) const {
        size_t i = pos_ + offset;
        return i < src_.size() ? src_[i] : '\0';
    }

    char advance() {
        char c = src_[pos_++];
        if (c == '\n') { ++line_; col_ = 1; } else ++col_;
        return c;
    }

    bool matchStr(const char* s) {
        size_t len = std::strlen(s);
        if (pos_ + len > src_.size()) return false;
        if (src_.compare(pos_, len, s) != 0) return false;
        for (size_t i = 0; i < len; ++i) advance();
        return true;
    }

    void skipLineRemainder() {
        while (!atEnd() && peek() != '\n' && peek() != '\r') advance();
    }

    void skipWS() {
        while (!atEnd()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') advance();
            else if (c == '#') { advance(); skipLineRemainder(); }
            else break;
        }
    }

    // Skip spaces/tabs only — does NOT cross newlines
    void skipInlineWS() {
        while (!atEnd() && (peek() == ' ' || peek() == '\t')) advance();
    }

    void error(const std::string& msg) {
        throw ParseError(filename_ + ":" + std::to_string(line_) + ":" +
                         std::to_string(col_) + ": " + msg, line_, col_);
    }

    void expect(char c) {
        skipWS();
        if (atEnd() || peek() != c)
            error(std::string("expected '") + c + "', got '" +
                  (atEnd() ? "EOF" : std::string(1, peek())) + "'");
        advance();
    }

    // ── Type declaration (§3.1) ───────────────────────────────────────────────

    void tryTypeDecl(Document& doc) {
        if (!opts_.allowTypeHeader) return;
        if (atEnd() || peek() != '@') return;
        size_t savePos = pos_; int saveLine = line_, saveCol = col_;
        advance(); // '@'
        if (matchStr("type:")) {
            std::string tid;
            // skip leading whitespace
            while (!atEnd() && (peek() == ' ' || peek() == '\t')) advance();
            while (!atEnd() && peek() != '\n' && peek() != '\r') tid += advance();
            // trim trailing whitespace
            while (!tid.empty() && std::isspace((unsigned char)tid.back())) tid.pop_back();
            if (tid.empty()) error("empty @type: identifier");
            doc.type = std::move(tid);
        } else {
            // not @type: — restore (could be @[ or line capture)
            pos_ = savePos; line_ = saveLine; col_ = saveCol;
        }
    }

    // ── Implicit object detection (§6.1) ─────────────────────────────────────

    bool isImplicitObject() const {
        if (atEnd()) return false;
        char c = peek();
        // Unambiguous implicit-object starters
        if (c == '&') return true;
        if (c == '.' && peek(1) == '.' && peek(2) == '.') return true;
        // If first char is not a potential key start, it's not implicit
        if (c == '@' && opts_.allowPropertyKeys) return true; // property key
        if (!std::isalpha((unsigned char)c) && c != '_' && c != '$' && c != '"' && c != '\'')
            return false;
        // Scan forward for ':' before any value-only delimiters
        for (size_t i = pos_; i < src_.size(); ++i) {
            char ch = src_[i];
            if (ch == ':') return true;
            if (ch == '\n' || ch == '{' || ch == '[') return false;
        }
        return false;
    }

    // ── Name / key ────────────────────────────────────────────────────────────

    std::string parseName() {
        // [a-zA-Z_$][a-zA-Z0-9_$-]*
        if (atEnd() || (!std::isalpha((unsigned char)peek()) && peek() != '_' && peek() != '$'))
            error("expected identifier");
        std::string name;
        while (!atEnd()) {
            char c = peek();
            if (std::isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-')
                name += advance();
            else break;
        }
        return name;
    }

    // ── ECMAScript identifier (no hyphens) ────────────────────────────────
    std::string parseECMAIdent() {
        if (atEnd() || (!std::isalpha((unsigned char)peek()) && peek() != '_' && peek() != '$'))
            error("expected ECMAScript identifier");
        std::string name;
        while (!atEnd()) {
            char c = peek();
            if (std::isalnum((unsigned char)c) || c == '_' || c == '$')
                name += advance();
            else break;
        }
        return name;
    }

    // ── Standard bare key: [a-zA-Z_$][a-zA-Z0-9_$-~%!]* ────────────────────
    std::string parseStandardKey() {
        if (atEnd() || (!std::isalpha((unsigned char)peek()) && peek() != '_' && peek() != '$'))
            error("expected bare key");
        std::string name;
        while (!atEnd()) {
            char c = peek();
            if (std::isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-'
                || c == '~' || c == '%' || c == '!')
                name += advance();
            else break;
        }
        return name;
    }

    std::string parseKey() {
        if (peek() == '"' || peek() == '\'') return parseQuotedString();
        // Property key: @ident (when enabled)
        if (opts_.allowPropertyKeys && peek() == '@') {
            advance(); // consume '@'
            std::string key = "@";
            key += parseBareKey();
            return key;
        }
        return parseBareKey();
    }

    std::string parseBareKey() {
        if (opts_.keyValidator) return parseCustomKey();
        switch (opts_.keyMode) {
            case KeyMode::ECMAScript: return parseECMAIdent();
            default:                  return parseStandardKey();
        }
    }

    std::string parseCustomKey() {
        std::string_view rest(src_.data() + pos_, src_.size() - pos_);
        size_t n = opts_.keyValidator(rest);
        if (n == 0 || n > rest.size()) error("expected key accepted by active key validator");
        std::string key;
        key.reserve(n);
        for (size_t i = 0; i < n; ++i) key += advance();
        return key;
    }

    // ── Value dispatch ────────────────────────────────────────────────────────

    Value parseValue(bool inStmt) {
        // When inStmt=true, line capture is a valid fallback.
        // Caller must have already called skipWS() or skipInlineWS() as appropriate.
        int vl = line_, vc = col_;

        if (atEnd()) { Value v; v.line=vl; v.col=vc; return v; } // null at EOF

        char c = peek();

        // ── Object ──────────────────────────────────────────────────────────
        if (c == '{') {
            advance();
            Value v = Value::obj_val(); v.line=vl; v.col=vc;
            parseBody(v, /*implicit=*/false);
            expect('}');
            return v;
        }

        // ── Array ────────────────────────────────────────────────────────────
        if (c == '[') return parseArray(vl, vc);

        // ── Described list or line capture ───────────────────────────────────
        if (c == '@') {
            if (peek(1) == '[') {
                advance(); // '@'
                return parseDescribed(vl, vc);
            }
            // '@' alone → line capture
            if (inStmt) return parseLineCapture(vl, vc);
            error("unexpected '@'");
        }

        // ── Triple-quoted string ─────────────────────────────────────────────
        if (c == '"' && peek(1) == '"' && peek(2) == '"') {
            Value v = Value::str_val(parseTripleString()); v.line=vl; v.col=vc; return v;
        }

        // ── Quoted string ────────────────────────────────────────────────────
        if (c == '"' || c == '\'') {
            Value v = Value::str_val(parseQuotedString()); v.line=vl; v.col=vc; return v;
        }

        // ── Number ───────────────────────────────────────────────────────────
        if (std::isdigit((unsigned char)c) ||
            (c == '-' && std::isdigit((unsigned char)peek(1)))) {
            // Lookahead: ensure the digit sequence isn't followed by alpha
            // chars (e.g. hex color "1a1a2e" should be line capture, not a
            // partial number parse consuming just "1").
            if (isCleanNumber()) {
                Value v = Value::num_val(parseNumber()); v.line=vl; v.col=vc; return v;
            }
            // Not a clean number — fall through to line capture below
        }

        // ── Anchor reference ─────────────────────────────────────────────────
        if (c == '*') {
            advance();
            std::string name = parseName();
            auto it = anchors_.find(name);
            if (it == anchors_.end()) error("undefined anchor: " + name);
            Value v = it->second; v.line=vl; v.col=vc; return v; // deep copy
        }

        // ── Boolean / null / bare string ─────────────────────────────────────
        if (std::isalpha((unsigned char)c) || c == '_' || c == '$') {
            if (matchKeyword("true"))  { Value v=Value::bool_val(true);  v.line=vl; v.col=vc; return v; }
            if (matchKeyword("false")) { Value v=Value::bool_val(false); v.line=vl; v.col=vc; return v; }
            if (matchKeyword("null"))  { Value v;                         v.line=vl; v.col=vc; return v; }

            // Try bare string: [a-zA-Z_$][a-zA-Z0-9_$-]* followed by a clean terminator
            std::string bs = tryBareString();
            if (!bs.empty()) {
                Value v=Value::str_val(bs); v.line=vl; v.col=vc; return v;
            }
            // Starts like a bare string but contains non-bare chars → line capture
            if (inStmt) return parseLineCapture(vl, vc);
            error("unexpected character: " + std::string(1, c));
        }

        // ── Line capture fallback ─────────────────────────────────────────────
        if (inStmt) return parseLineCapture(vl, vc);

        error("unexpected character: " + std::string(1, c));
        return Value{}; // unreachable
    }

    bool matchKeyword(const char* kw) {
        size_t len = std::strlen(kw);
        if (pos_ + len > src_.size()) return false;
        if (src_.compare(pos_, len, kw) != 0) return false;
        char next = (pos_ + len < src_.size()) ? src_[pos_ + len] : '\0';
        if (std::isalnum((unsigned char)next) || next == '_' || next == '$' || next == '-')
            return false;
        for (size_t i = 0; i < len; ++i) advance();
        return true;
    }

    // Returns the bare string if the next chars form one, "" otherwise.
    // Bare string: [a-zA-Z_$][a-zA-Z0-9_$-]* terminated by whitespace/comma/bracket/#/EOF
    std::string tryBareString() {
        // Bare string: [a-zA-Z_$][a-zA-Z0-9_$-]* with no embedded whitespace.
        // After the bare chars, skip inline whitespace and check the next char.
        // If a structural delimiter follows, it's a bare string.
        // If more visible content follows (e.g. "SELECT id FROM..."), it's a line capture.
        size_t i = pos_;
        while (i < src_.size()) {
            char c = src_[i];
            if (std::isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-') ++i;
            else break;
        }
        if (i == pos_) return "";
        // Skip trailing inline whitespace to find the real next char
        size_t j = i;
        while (j < src_.size() && (src_[j] == ' ' || src_[j] == '\t')) ++j;
        char after = j < src_.size() ? src_[j] : '\0';
        // Structural delimiters that cleanly end a bare string value
        if (after == '\n' || after == '\r' || after == ',' ||
            after == '}' || after == ']' || after == '#' || after == '\0') {
            std::string s = src_.substr(pos_, i - pos_);
            while (pos_ < i) advance();
            return s;
        }
        // More content on this line after the word — it's a line capture
        return "";
    }

    // ── Line capture (§4.4) ───────────────────────────────────────────────────

    Value parseLineCapture(int vl, int vc) {
        std::string captured;
        while (!atEnd()) {
            char c = peek();
            if (c == '\n' || c == '\r') break;
            if (c == ',') break;
            if (c == '#') {
                // '#' preceded by whitespace (or at start) → comment; stop
                // '#' embedded in non-whitespace → literal
                bool preceded_by_ws = !captured.empty() &&
                    (captured.back() == ' ' || captured.back() == '	');
                if (preceded_by_ws) { advance(); skipLineRemainder(); break; }
                else captured += advance();
                continue;
            }
            captured += advance();
        }
        // Trim trailing whitespace
        while (!captured.empty() &&
               (captured.back() == ' ' || captured.back() == '\t')) captured.pop_back();
        Value v = Value::str_val(captured, /*isLineCapture=*/true);
        v.line = vl; v.col = vc;
        return v;
    }

    // ── Array (§5.5) ─────────────────────────────────────────────────────────

    Value parseArray(int vl, int vc) {
        advance(); // '['
        Value arr = Value::arr_val(); arr.line=vl; arr.col=vc;
        skipWS();
        while (!atEnd() && peek() != ']') {
            arr.array.push_back(parseValue(/*inStmt=*/false));
            skipWS();
            if (!atEnd() && peek() == ',') { advance(); skipWS(); }
            // No break on missing comma — CXD allows whitespace-separated array elements
        }
        if (atEnd()) error("unclosed array");
        advance(); // ']'
        return arr;
    }

    // ── Described list (§5.6) ─────────────────────────────────────────────────

    Value parseDescribed(int vl, int vc) {
        // '@' already consumed; now '[' is next
        advance(); // '['
        std::vector<std::string> fields;
        skipWS();
        while (!atEnd() && peek() != ']') {
            fields.push_back(parseKey());
            skipWS();
            if (!atEnd() && peek() == ',') { advance(); skipWS(); }
            else break;
        }
        if (atEnd()) error("unclosed described list header");
        advance(); // ']'

        skipWS();
        if (atEnd() || peek() != '[') error("expected '[' after described list header");
        advance(); // outer '['

        Value result = Value::arr_val(); result.line=vl; result.col=vc;
        skipWS();
        while (!atEnd() && peek() != ']') {
            if (peek() != '[') error("expected inner array in described list");
            advance(); // inner '['
            Value obj = Value::obj_val();
            size_t fi = 0;
            skipWS();
            while (!atEnd() && peek() != ']') {
                if (fi >= fields.size())
                    error("described list row has more values than fields");
                obj.object.push_back({fields[fi++], parseValue(/*inStmt=*/false)});
                skipWS();
                if (!atEnd() && peek() == ',') { advance(); skipWS(); }
                else break;
            }
            if (atEnd()) error("unclosed inner array in described list");
            advance(); // inner ']'
            // Fill missing fields with null
            while (fi < fields.size()) obj.object.push_back({fields[fi++], Value{}});
            result.array.push_back(std::move(obj));
            skipWS();
            if (!atEnd() && peek() == ',') { advance(); skipWS(); }
            else break;
        }
        if (atEnd()) error("unclosed described list array");
        advance(); // outer ']'
        return result;
    }

    // ── Object body (§5.7) ────────────────────────────────────────────────────

    void parseBody(Value& obj, bool implicit_) {
        skipWS();
        while (true) {
            skipWS();
            if (atEnd()) break;
            if (!implicit_ && peek() == '}') break;

            // ── Anchor declaration: &name [=] value ──────────────────────────
            if (peek() == '&') {
                advance();
                std::string name = parseName();
                skipWS();
                if (!atEnd() && peek() == '=') { advance(); skipWS(); }
                Value v = parseValue(/*inStmt=*/true);
                anchors_[name] = v; // stored but not emitted to output
                skipWS();
                if (!atEnd() && peek() == ',') { advance(); }
                continue;
            }

            // ── Spread: ...name | ...*name | ...{...} ────────────────────────
            if (peek() == '.' && peek(1) == '.' && peek(2) == '.') {
                advance(); advance(); advance(); // '...'
                Value target;
                if (!atEnd() && peek() == '*') {
                    advance();
                    std::string name = parseName();
                    auto it = anchors_.find(name);
                    if (it == anchors_.end()) error("undefined anchor: " + name);
                    target = it->second;
                } else if (!atEnd() && peek() == '{') {
                    advance();
                    target = Value::obj_val();
                    parseBody(target, false);
                    expect('}');
                } else {
                    std::string name = parseName();
                    auto it = anchors_.find(name);
                    if (it == anchors_.end()) error("undefined anchor: " + name);
                    target = it->second;
                }
                if (!target.isObject()) error("spread of non-object value");
                for (auto& [k, v] : target.object) obj.set(k, v);
                skipWS();
                if (!atEnd() && peek() == ',') { advance(); }
                continue;
            }

            // ── Key : value ───────────────────────────────────────────────────
            std::string key = parseKey();
            skipWS();
            if (atEnd() || peek() != ':')
                error("expected ':' after key '" + key + "'");
            advance(); // ':'
            // Skip inline whitespace only — do NOT call skipWS() here because
            // skipWS() treats '#' as a comment, which would eat "#FF0000"-style
            // line-capture values. Only cross a newline if the current line is
            // completely empty (newline, comment, or EOF follows immediately).
            skipInlineWS();
            Value v;
            if (atEnd() || peek() == '\n' || peek() == '\r') {
                // Nothing on this line; value starts on next line (not a line capture)
                skipWS();
                v = parseValue(/*inStmt=*/false);
            } else {
                // Content on this line — line capture is possible
                v = parseValue(/*inStmt=*/true);
            }
            obj.set(key, std::move(v));

            skipWS();
            if (!atEnd() && peek() == ',') { advance(); }
            // no else-error: tolerate missing trailing comma before '}'/EOF
        }
    }

    // ── Strings ───────────────────────────────────────────────────────────────

    std::string parseQuotedString() {
        char q = advance(); // opening ' or "
        std::string s;
        while (!atEnd()) {
            char c = peek();
            if (c == q) { advance(); return s; }
            if (c == '\n' || c == '\r') error("unclosed string literal");
            if (c == '\\') {
                advance();
                char esc = atEnd() ? '\0' : advance();
                switch (esc) {
                    case '\\': s += '\\'; break;
                    case '"':  s += '"';  break;
                    case '\'': s += '\''; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (atEnd()) error("incomplete \\u escape");
                            char h = advance();
                            cp <<= 4;
                            if (h>='0'&&h<='9')      cp|=(h-'0');
                            else if (h>='a'&&h<='f') cp|=(h-'a'+10);
                            else if (h>='A'&&h<='F') cp|=(h-'A'+10);
                            else error("invalid hex digit in \\u escape");
                        }
                        s += utf8Encode(cp);
                        break;
                    }
                    default: s += '\\'; s += esc; break;
                }
            } else {
                s += advance();
            }
        }
        error("unclosed string literal");
        return {};
    }

    // Triple-quoted string: """…""" with automatic indent stripping (§5.4.2)
    std::string parseTripleString() {
        advance(); advance(); advance(); // '"""'
        // Consume optional newline immediately after opening """
        if (!atEnd() && peek() == '\r') advance();
        if (!atEnd() && peek() == '\n') advance();

        std::vector<std::string> lines;
        std::string cur;
        while (!atEnd()) {
            if (peek()=='"' && peek(1)=='"' && peek(2)=='"') {
                lines.push_back(cur); advance(); advance(); advance(); break;
            }
            if (peek()=='\r') { advance(); continue; } // skip CR
            if (peek()=='\n') { advance(); lines.push_back(cur); cur.clear(); continue; }
            cur += advance();
        }

        // Drop final empty line (the one before closing """)
        if (!lines.empty() && lines.back().empty()) lines.pop_back();

        // Find common leading whitespace across non-empty lines
        size_t common = std::string::npos;
        for (auto& ln : lines) {
            if (ln.empty()) continue;
            size_t lead = 0;
            while (lead < ln.size() && (ln[lead]==' ' || ln[lead]=='\t')) ++lead;
            if (common == std::string::npos || lead < common) common = lead;
        }
        if (common == std::string::npos) common = 0;

        std::string result;
        for (size_t i = 0; i < lines.size(); ++i) {
            auto& ln = lines[i];
            result += (ln.size() >= common) ? ln.substr(common) : ln;
            if (i + 1 < lines.size()) result += '\n';
        }
        return result;
    }

    // ── Numbers (§5.3) ────────────────────────────────────────────────────────

    // Lookahead: scan digit sequence (with 0x/0b/0o prefixes and decimals)
    // and return false if it's followed by alpha chars, indicating a
    // non-numeric token like a hex color "1a1a2e" or identifier "3dModel".
    bool isCleanNumber() const {
        size_t i = pos_;
        if (i < src_.size() && src_[i] == '-') ++i;
        if (i >= src_.size()) return false;
        // 0x/0b/0o prefixes — these legitimately contain alpha
        if (src_[i] == '0' && i + 1 < src_.size()) {
            char r = src_[i + 1];
            if (r == 'x' || r == 'X' || r == 'b' || r == 'B' || r == 'o' || r == 'O')
                return true; // trust parseNumber to handle these
        }
        // Scan digits (with grouping spaces and decimal point)
        while (i < src_.size() && (std::isdigit((unsigned char)src_[i]) || src_[i] == '.')) ++i;
        // Exponent
        if (i < src_.size() && (src_[i] == 'e' || src_[i] == 'E')) {
            ++i;
            if (i < src_.size() && (src_[i] == '+' || src_[i] == '-')) ++i;
            while (i < src_.size() && std::isdigit((unsigned char)src_[i])) ++i;
        }
        // If followed by alpha or underscore, it's not a clean number
        if (i < src_.size() && (std::isalpha((unsigned char)src_[i]) || src_[i] == '_'))
            return false;
        return true;
    }

    double parseNumber() {
        bool neg = false;
        if (peek() == '-') { advance(); neg = true; }

        // Extended literals
        if (peek() == '0') {
            char r = peek(1);
            if (r == 'x' || r == 'X') { advance(); advance(); return neg ? -parseHex()    : parseHex(); }
            if (r == 'b' || r == 'B') { advance(); advance(); return neg ? -parseBin()    : parseBin(); }
            if (r == 'o' || r == 'O') { advance(); advance(); return neg ? -parseOct()    : parseOct(); }
        }

        // Decimal with space grouping
        std::string num;
        if (!std::isdigit((unsigned char)peek())) error("expected digit");
        num += advance();
        while (!atEnd()) {
            char c = peek();
            if (std::isdigit((unsigned char)c)) { num += advance(); }
            else if (c == ' ' && pos_+1<src_.size() && std::isdigit((unsigned char)src_[pos_+1])) {
                advance(); // skip grouping space
            } else break;
        }
        // Fractional
        if (!atEnd() && peek() == '.' && pos_+1<src_.size() && std::isdigit((unsigned char)src_[pos_+1])) {
            num += advance(); // '.'
            while (!atEnd()) {
                char c = peek();
                if (std::isdigit((unsigned char)c)) { num += advance(); }
                else if (c == ' ' && pos_+1<src_.size() && std::isdigit((unsigned char)src_[pos_+1])) {
                    advance();
                } else break;
            }
        }
        // Exponent
        if (!atEnd() && (peek()=='e'||peek()=='E')) {
            num += advance();
            if (!atEnd() && (peek()=='+'||peek()=='-')) num += advance();
            while (!atEnd() && std::isdigit((unsigned char)peek())) num += advance();
        }
        double v = std::stod(num);
        return neg ? -v : v;
    }

    double parseHex() {
        std::string d;
        while (!atEnd() && (std::isxdigit((unsigned char)peek()) || peek()=='_'))
            if (peek()!='_') d += advance(); else advance();
        if (d.empty()) error("empty hex literal");
        return (double)std::stoull(d, nullptr, 16);
    }
    double parseBin() {
        std::string d;
        while (!atEnd() && (peek()=='0'||peek()=='1'||peek()=='_'))
            if (peek()!='_') d += advance(); else advance();
        if (d.empty()) error("empty binary literal");
        return (double)std::stoull(d, nullptr, 2);
    }
    double parseOct() {
        std::string d;
        while (!atEnd() && ((peek()>='0'&&peek()<='7')||peek()=='_'))
            if (peek()!='_') d += advance(); else advance();
        if (d.empty()) error("empty octal literal");
        return (double)std::stoull(d, nullptr, 8);
    }

    // ── Utilities ─────────────────────────────────────────────────────────────

    std::string utf8Encode(uint32_t cp) {
        std::string s;
        if (cp < 0x80) { s += (char)cp; }
        else if (cp < 0x800) {
            s += (char)(0xC0|(cp>>6));
            s += (char)(0x80|(cp&0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0|(cp>>12));
            s += (char)(0x80|((cp>>6)&0x3F));
            s += (char)(0x80|(cp&0x3F));
        } else {
            s += (char)(0xF0|(cp>>18));
            s += (char)(0x80|((cp>>12)&0x3F));
            s += (char)(0x80|((cp>>6)&0x3F));
            s += (char)(0x80|(cp&0x3F));
        }
        return s;
    }
};

// ─── Public parse functions ───────────────────────────────────────────────────

Document parse(const std::string& source, const std::string& filename, ParseOptions opts) {
    Parser p(source, filename, opts);
    return p.parse();
}

Document parseFile(const std::string& path, ParseOptions opts) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cxd: cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), path, opts);
}

// ─── JSON serialization ───────────────────────────────────────────────────────

static void jsonEscape(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8]; snprintf(buf,sizeof(buf),"\\u%04X",(unsigned)c);
            out += buf;
        } else out += (char)c;
    }
    out += '"';
}

static void jsonWrite(const Value& v, std::string& out, int indent, int depth) {
    std::string pad(depth * indent, ' ');
    std::string childPad((depth+1) * indent, ' ');
    switch (v.kind) {
    case Kind::Null:   out += "null"; break;
    case Kind::Bool:   out += v.boolean ? "true" : "false"; break;
    case Kind::Number:
        if (v.number == std::floor(v.number) && std::abs(v.number) < 1e15 &&
            !std::isinf(v.number)) {
            out += std::to_string((long long)v.number);
        } else {
            char buf[32]; snprintf(buf,sizeof(buf),"%.17g",v.number); out += buf;
        }
        break;
    case Kind::String: jsonEscape(v.string, out); break;
    case Kind::Array:
        if (v.array.empty()) { out += "[]"; break; }
        out += (indent>0 ? "[\n" : "[");
        for (size_t i=0; i<v.array.size(); ++i) {
            if (indent>0) out += childPad;
            jsonWrite(v.array[i], out, indent, depth+1);
            if (i+1<v.array.size()) out += ',';
            if (indent>0) out += '\n';
        }
        if (indent>0) out += pad;
        out += ']';
        break;
    case Kind::Object:
        if (v.object.empty()) { out += "{}"; break; }
        out += (indent>0 ? "{\n" : "{");
        for (size_t i=0; i<v.object.size(); ++i) {
            if (indent>0) out += childPad;
            jsonEscape(v.object[i].first, out);
            out += (indent>0 ? ": " : ":");
            jsonWrite(v.object[i].second, out, indent, depth+1);
            if (i+1<v.object.size()) out += ',';
            if (indent>0) out += '\n';
        }
        if (indent>0) out += pad;
        out += '}';
        break;
    }
}

std::string toJson(const Value& v, int indent) {
    std::string out;
    jsonWrite(v, out, indent, 0);
    return out;
}

} // namespace cxd
