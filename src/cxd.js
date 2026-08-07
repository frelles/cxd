/**
 * CXD Parser — Component Extensible Data
 * Spec version: 0.99.001
 *
 * Produces a JSON-compatible data model from CXD source text.
 * Supports: implicit top-level object, bare/quoted/triple-quoted strings,
 * numbers (space-grouped decimals, hex, binary, octal), arrays, objects,
 * described lists, anchors & spread, line capture, @type declaration,
 * # comments, property keys (opt-in).
 *
 * Usage:
 *   const result = CXD.parse(source, options);
 *   // result.data  — the parsed value
 *   // result.type  — @type declaration or null
 *
 * Options:
 *   keyValidator: 'standard' | 'ecmascript' | function(key) => boolean
 *   propertyKeys: true | false (default false)
 */
(function (root, factory) {
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else if (typeof define === 'function' && define.amd) define(factory);
    else root.CXD = factory();
}(typeof globalThis !== 'undefined' ? globalThis : typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    // ─── Character helpers ───────────────────────────────────────────

    function isDigit(c) { return c >= '0' && c <= '9'; }
    function isHexDigit(c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    function isWhitespace(c) { return c === ' ' || c === '\t' || c === '\r' || c === '\n'; }
    function isNewline(c) { return c === '\n' || c === '\r'; }
    function isKeyStart(c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c === '_' || c === '$'; }
    function isKeyChar(c) { return isKeyStart(c) || isDigit(c) || c === '-'; }

    // ─── Key validators ──────────────────────────────────────────────

    const validators = {
        standard: function (k) {
            if (!k.length || !isKeyStart(k[0])) return false;
            for (let i = 1; i < k.length; i++) if (!isKeyChar(k[i])) return false;
            return true;
        },
        ecmascript: function (k) {
            if (!k.length || !isKeyStart(k[0])) return false;
            for (let i = 1; i < k.length; i++) {
                const c = k[i];
                if (!isKeyStart(c) && !isDigit(c)) return false;
            }
            return true;
        }
    };

    // ─── Error helper ────────────────────────────────────────────────

    function error(msg, pos, src) {
        let line = 1, col = 1;
        for (let i = 0; i < pos && i < src.length; i++) {
            if (src[i] === '\n') { line++; col = 1; } else col++;
        }
        const e = new SyntaxError('CXD: ' + msg + ' at line ' + line + ', col ' + col);
        e.line = line;
        e.col = col;
        e.offset = pos;
        throw e;
    }

    // ─── Parser ──────────────────────────────────────────────────────

    function parse(source, options) {
        options = options || {};
        const src = source;
        const len = src.length;
        let pos = 0;
        const anchors = {};
        const propertyKeys = !!options.propertyKeys;

        let validateKey;
        if (typeof options.keyValidator === 'function') validateKey = options.keyValidator;
        else if (options.keyValidator && validators[options.keyValidator]) validateKey = validators[options.keyValidator];
        else validateKey = validators.standard;

        // ── Whitespace / comments ────────────────────────────────────

        function skipWS(preserveComments) {
            while (pos < len) {
                const c = src[pos];
                if (isWhitespace(c)) { pos++; continue; }
                if (!preserveComments && c === '#') {
                    while (pos < len && src[pos] !== '\n') pos++;
                    continue;
                }
                break;
            }
        }

        function peek() { return pos < len ? src[pos] : ''; }
        function at(offset) { return (pos + offset) < len ? src[pos + offset] : ''; }
        function advance() { return src[pos++]; }
        function expect(ch) {
            if (src[pos] !== ch) error('Expected "' + ch + '" but got "' + (src[pos] || 'EOF') + '"', pos, src);
            pos++;
        }

        // ── @type declaration ────────────────────────────────────────

        let docType = null;

        function parseTypeDecl() {
            const saved = pos;
            skipWS();
            if (pos + 6 <= len && src.substring(pos, pos + 6) === '@type:') {
                pos += 6;
                let id = '';
                while (pos < len && !isNewline(src[pos])) {
                    id += src[pos++];
                }
                docType = id.trim();
                return;
            }
            pos = saved;
        }

        // ── Numbers ──────────────────────────────────────────────────

        function parseNumber() {
            const start = pos;
            let negative = false;

            if (src[pos] === '-') { negative = true; pos++; }

            // Extended literals
            if (src[pos] === '0' && pos + 1 < len) {
                const prefix = src[pos + 1];
                if (prefix === 'x' || prefix === 'X') {
                    pos += 2;
                    let hex = '';
                    while (pos < len && (isHexDigit(src[pos]) || src[pos] === '_')) {
                        if (src[pos] !== '_') hex += src[pos];
                        pos++;
                    }
                    if (!hex.length) error('Empty hex literal', start, src);
                    return (negative ? -1 : 1) * parseInt(hex, 16);
                }
                if (prefix === 'b' || prefix === 'B') {
                    pos += 2;
                    let bin = '';
                    while (pos < len && (src[pos] === '0' || src[pos] === '1' || src[pos] === '_')) {
                        if (src[pos] !== '_') bin += src[pos];
                        pos++;
                    }
                    if (!bin.length) error('Empty binary literal', start, src);
                    return (negative ? -1 : 1) * parseInt(bin, 2);
                }
                if (prefix === 'o' || prefix === 'O') {
                    pos += 2;
                    let oct = '';
                    while (pos < len && ((src[pos] >= '0' && src[pos] <= '7') || src[pos] === '_')) {
                        if (src[pos] !== '_') oct += src[pos];
                        pos++;
                    }
                    if (!oct.length) error('Empty octal literal', start, src);
                    return (negative ? -1 : 1) * parseInt(oct, 8);
                }
            }

            // Decimal — with space grouping
            let numStr = '';

            // Integer part
            while (pos < len) {
                if (isDigit(src[pos])) {
                    numStr += src[pos++];
                } else if (src[pos] === ' ' && pos + 1 < len && isDigit(src[pos + 1])) {
                    // Space grouping — only between two digits
                    pos++;
                } else break;
            }

            // Fractional part
            if (pos < len && src[pos] === '.') {
                numStr += '.';
                pos++;
                while (pos < len) {
                    if (isDigit(src[pos])) {
                        numStr += src[pos++];
                    } else if (src[pos] === ' ' && pos + 1 < len && isDigit(src[pos + 1])) {
                        pos++;
                    } else break;
                }
            }

            // Exponent
            if (pos < len && (src[pos] === 'e' || src[pos] === 'E')) {
                numStr += src[pos++];
                if (pos < len && (src[pos] === '+' || src[pos] === '-')) numStr += src[pos++];
                while (pos < len && isDigit(src[pos])) numStr += src[pos++];
            }

            const n = Number(numStr);
            return negative ? -n : n;
        }

        // ── Strings ──────────────────────────────────────────────────

        function parseEscape(quote) {
            pos++; // skip backslash
            const c = src[pos++];
            switch (c) {
                case '\\': return '\\';
                case '"': return '"';
                case "'": return "'";
                case 'n': return '\n';
                case 'r': return '\r';
                case 't': return '\t';
                case 'u': {
                    let hex = src.substring(pos, pos + 4);
                    pos += 4;
                    return String.fromCharCode(parseInt(hex, 16));
                }
                default: return '\\' + c;
            }
        }

        function parseQuotedString(quote) {
            pos++; // opening quote
            let result = '';
            while (pos < len) {
                if (src[pos] === '\\') {
                    result += parseEscape(quote);
                } else if (src[pos] === quote) {
                    pos++;
                    return result;
                } else {
                    result += src[pos++];
                }
            }
            error('Unclosed string', pos, src);
        }

        function parseTripleString() {
            pos += 3; // skip opening """
            // skip to end of opening line
            while (pos < len && src[pos] !== '\n') pos++;
            if (pos < len) pos++; // skip newline

            let lines = [];
            let body = '';
            while (pos < len) {
                if (pos + 3 <= len && src.substring(pos, pos + 3) === '"""') {
                    // check closing is on its own line (only whitespace before)
                    break;
                }
                // look for """ preceded only by whitespace on this line
                let lineStart = pos;
                let line = '';
                while (pos < len && src[pos] !== '\n') {
                    line += src[pos++];
                }
                // check if this line is the closing
                if (line.trimStart().startsWith('"""')) {
                    // rewind to find the """
                    pos = lineStart;
                    while (pos < len && src[pos] !== '"') pos++;
                    pos += 3; // skip closing """
                    break;
                }
                lines.push(line);
                if (pos < len) pos++; // skip newline
            }

            // Strip common indent
            let minIndent = Infinity;
            for (const l of lines) {
                if (l.trim().length === 0) continue;
                let indent = 0;
                for (const c of l) {
                    if (c === ' ') indent++;
                    else if (c === '\t') indent += 4;
                    else break;
                }
                if (indent < minIndent) minIndent = indent;
            }
            if (minIndent === Infinity) minIndent = 0;

            const stripped = lines.map(l => {
                if (l.trim().length === 0) return '';
                return l.substring(minIndent);
            });

            // Remove trailing empty lines
            while (stripped.length && stripped[stripped.length - 1] === '') stripped.pop();

            return stripped.join('\n');
        }

        // ── Bare string ──────────────────────────────────────────────

        function parseBareString() {
            let result = '';
            while (pos < len && isKeyChar(src[pos])) {
                result += src[pos++];
            }
            return result;
        }

        // ── Line capture ─────────────────────────────────────────────

        function parseLineCapture() {
            let result = '';
            while (pos < len) {
                const c = src[pos];
                if (c === '\n' || c === '\r') break;
                if (c === ',') {
                    // Check if comma is a structural separator or part of line capture.
                    // In line capture mode, comma ends the value.
                    break;
                }
                if (c === '#') {
                    // Comment resolution per §4.5.4:
                    // # is a comment only when preceded by comma or isolating whitespace AFTER a token
                    const trimmed = result.trimEnd();
                    if (trimmed.length > 0 && (trimmed.endsWith(',') || result.endsWith(' ') || result.endsWith('\t'))) {
                        // Treat as comment — there's preceding content with trailing space/comma
                        break;
                    }
                    // Inline literal — absorb
                    result += src[pos++];
                    continue;
                }
                result += src[pos++];
            }
            return result.trim();
        }

        // ── Deep clone ───────────────────────────────────────────────

        function deepClone(v) {
            if (v === null || typeof v !== 'object') return v;
            if (Array.isArray(v)) return v.map(deepClone);
            const o = {};
            for (const k in v) if (v.hasOwnProperty(k)) o[k] = deepClone(v[k]);
            return o;
        }

        // ── Key parsing ──────────────────────────────────────────────

        function tryParseKey() {
            skipWS();
            if (pos >= len) return null;

            let propPrefix = false;
            if (propertyKeys && src[pos] === '@' && pos + 1 < len && isKeyStart(src[pos + 1])) {
                propPrefix = true;
                pos++;
            }

            let key;
            if (src[pos] === '"' || src[pos] === "'") {
                key = parseQuotedString(src[pos]);
            } else if (isKeyStart(src[pos])) {
                let start = pos;
                while (pos < len && isKeyChar(src[pos])) pos++;
                key = src.substring(start, pos);
                if (!validateKey(key)) {
                    pos = start;
                    return null;
                }
            } else {
                if (propPrefix) pos--; // unconsume @
                return null;
            }

            if (propPrefix) key = '@' + key;
            return key;
        }

        // ── Value dispatch ───────────────────────────────────────────

        function parseValue(inStatement) {
            skipWS();
            if (pos >= len) error('Unexpected end of input', pos, src);
            const c = src[pos];

            // Triple-quoted string
            if (c === '"' && pos + 2 < len && src[pos + 1] === '"' && src[pos + 2] === '"') {
                return parseTripleString();
            }

            // Quoted string
            if (c === '"' || c === "'") return parseQuotedString(c);

            // Object
            if (c === '{') return parseObject();

            // Array or described list
            if (c === '[') return parseArray();

            // Described list: @[...]
            if (c === '@' && at(1) === '[') return parseDescribedList();

            // Anchor reference
            if (c === '*') return parseAnchorRef();

            // Number
            if (isDigit(c) || (c === '-' && pos + 1 < len && isDigit(src[pos + 1]))) {
                return parseNumber();
            }

            // Keywords and bare strings
            if (isKeyStart(c)) {
                const saved = pos;
                let word = '';
                while (pos < len && isKeyChar(src[pos])) word += src[pos++];

                if (word === 'true') return true;
                if (word === 'false') return false;
                if (word === 'null') return null;

                // Check if this is a bare string (no structural chars follow within the word)
                // Bare string: only [a-zA-Z0-9_$-]
                let isBare = true;
                for (let i = 0; i < word.length; i++) {
                    if (!isKeyChar(word[i])) { isBare = false; break; }
                }
                if (isBare) return word;

                // Not a bare string — if in statement context, try line capture
                pos = saved;
            }

            // Line capture — only valid in statement context (after key:)
            if (inStatement) {
                return parseLineCapture();
            }

            error('Unexpected character "' + c + '"', pos, src);
        }

        // ── Array ────────────────────────────────────────────────────

        function parseArray() {
            expect('[');
            const result = [];
            skipWS();
            while (pos < len && src[pos] !== ']') {
                result.push(parseValue(false));
                skipWS();
                if (src[pos] === ',') { pos++; skipWS(); }
            }
            expect(']');
            return result;
        }

        // ── Described list ───────────────────────────────────────────

        function parseDescribedList() {
            pos++; // skip @
            expect('[');
            const fields = [];
            skipWS();
            while (pos < len && src[pos] !== ']') {
                const key = tryParseKey();
                if (key === null) error('Expected field name in described list', pos, src);
                fields.push(key);
                skipWS();
                if (src[pos] === ',') { pos++; skipWS(); }
            }
            expect(']');
            skipWS();

            // Now parse the array of arrays
            const rows = parseArray();
            const result = [];
            for (const row of rows) {
                if (!Array.isArray(row)) error('Described list rows must be arrays', pos, src);
                if (row.length > fields.length) error('Described list row has more values than fields', pos, src);
                const obj = {};
                for (let i = 0; i < fields.length; i++) {
                    obj[fields[i]] = i < row.length ? row[i] : null;
                }
                result.push(obj);
            }
            return result;
        }

        // ── Object ───────────────────────────────────────────────────

        function parseObject() {
            expect('{');
            const obj = parseBody();
            skipWS();
            expect('}');
            return obj;
        }

        function parseBody() {
            const obj = {};
            skipWS();
            while (pos < len) {
                skipWS();
                if (pos >= len) break;
                const c = src[pos];

                // End of object
                if (c === '}') break;

                // Anchor declaration: &name [=] value
                if (c === '&') {
                    pos++;
                    const name = parseBareString();
                    if (!name.length) error('Expected anchor name', pos, src);
                    skipWS();
                    if (src[pos] === '=') pos++;
                    skipWS();
                    anchors[name] = parseValue(false);
                    skipWS();
                    if (src[pos] === ',') { pos++; }
                    continue;
                }

                // Spread: ...name or ...*name or ...{obj}
                if (c === '.' && at(1) === '.' && at(2) === '.') {
                    pos += 3;
                    skipWS();
                    let spreadObj;
                    if (src[pos] === '*') {
                        spreadObj = parseAnchorRef();
                    } else if (src[pos] === '{') {
                        spreadObj = parseObject();
                    } else {
                        // bare anchor name
                        const name = parseBareString();
                        if (!anchors.hasOwnProperty(name)) error('Unknown anchor "' + name + '"', pos, src);
                        spreadObj = deepClone(anchors[name]);
                    }
                    if (typeof spreadObj !== 'object' || spreadObj === null || Array.isArray(spreadObj)) {
                        error('Spread target must be an object', pos, src);
                    }
                    for (const k in spreadObj) if (spreadObj.hasOwnProperty(k)) obj[k] = spreadObj[k];
                    skipWS();
                    if (src[pos] === ',') { pos++; }
                    continue;
                }

                // Key-value pair
                const saved = pos;
                const key = tryParseKey();
                if (key === null) {
                    // If nothing matches, we're done
                    break;
                }
                skipWS();
                if (src[pos] !== ':') {
                    // Not a key:value — revert
                    pos = saved;
                    break;
                }
                pos++; // skip colon
                // Don't skipWS here — parseStmtValue handles it with comment preservation

                // Parse statement value with line capture fallback
                obj[key] = parseStmtValue();

                skipWS();
                if (src[pos] === ',') { pos++; }
            }
            return obj;
        }

        // ── Statement value (with line capture fallback) ─────────────

        function parseStmtValue() {
            skipWS(true); // preserve # — it may be a line capture value like #FF0000
            if (pos >= len) error('Unexpected end of input after ":"', pos, src);
            const c = src[pos];

            // Triple-quoted string
            if (c === '"' && pos + 2 < len && src[pos + 1] === '"' && src[pos + 2] === '"') {
                return parseTripleString();
            }

            // Quoted string
            if (c === '"' || c === "'") return parseQuotedString(c);

            // Object
            if (c === '{') return parseObject();

            // Array
            if (c === '[') return parseArray();

            // Described list: @[
            if (c === '@' && at(1) === '[') return parseDescribedList();

            // Anchor reference
            if (c === '*') return parseAnchorRef();

            // Number
            if (isDigit(c) || (c === '-' && pos + 1 < len && isDigit(src[pos + 1]))) {
                // Attempt number parse — but validate it's actually a clean number
                const saved = pos;
                const num = parseNumber();
                // Check what follows — if it's still part of a token, it's line capture
                if (pos < len && !isWhitespace(src[pos]) && src[pos] !== ',' && src[pos] !== '}' && src[pos] !== ']' && src[pos] !== '#') {
                    // Not a clean number boundary — rewind and line capture
                    pos = saved;
                    return parseLineCapture();
                }
                return num;
            }

            // Keywords
            if (isKeyStart(c)) {
                const saved = pos;
                let word = '';
                while (pos < len && isKeyChar(src[pos])) word += src[pos++];

                if (word === 'true') return true;
                if (word === 'false') return false;
                if (word === 'null') return null;

                // Check if bare string — only keyChars and next char is structural
                skipWS();
                const next = pos < len ? src[pos] : '';
                if (next === ',' || next === '}' || next === ']' || next === '#' || next === '' || isNewline(next) || pos >= len) {
                    return word;
                }

                // Contains more — line capture from the beginning
                pos = saved;
                return parseLineCapture();
            }

            // Anything else — line capture
            return parseLineCapture();
        }

        // ── Anchor reference ─────────────────────────────────────────

        function parseAnchorRef() {
            pos++; // skip *
            const name = parseBareString();
            if (!anchors.hasOwnProperty(name)) error('Unknown anchor "' + name + '"', pos, src);
            return deepClone(anchors[name]);
        }

        // ── Implicit object detection ────────────────────────────────

        function isImplicitObject() {
            const saved = pos;
            skipWS();
            if (pos >= len) { pos = saved; return false; }

            const c = src[pos];

            // Anchor or spread
            if (c === '&') { pos = saved; return true; }
            if (c === '.' && at(1) === '.' && at(2) === '.') { pos = saved; return true; }

            // Property key
            if (propertyKeys && c === '@' && pos + 1 < len && isKeyStart(src[pos + 1])) {
                pos++;
                // skip key chars
                while (pos < len && isKeyChar(src[pos])) pos++;
                skipWS();
                const isKV = pos < len && src[pos] === ':';
                pos = saved;
                return isKV;
            }

            // Bare key or quoted key followed by colon
            if (isKeyStart(c)) {
                while (pos < len && isKeyChar(src[pos])) pos++;
                skipWS();
                const isKV = pos < len && src[pos] === ':';
                pos = saved;
                return isKV;
            }
            if (c === '"' || c === "'") {
                const q = c;
                pos++;
                while (pos < len && src[pos] !== q) {
                    if (src[pos] === '\\') pos++;
                    pos++;
                }
                if (pos < len) pos++; // close quote
                skipWS();
                const isKV = pos < len && src[pos] === ':';
                pos = saved;
                return isKV;
            }

            pos = saved;
            return false;
        }

        // ── Entry point ──────────────────────────────────────────────

        parseTypeDecl();

        let data;
        skipWS();
        if (pos >= len) {
            data = {};
        } else if (isImplicitObject()) {
            data = parseBody();
        } else {
            data = parseValue(false);
        }

        return { data: data, type: docType };
    }

    // ── Public API ───────────────────────────────────────────────────

    return {
        parse: parse,
        version: '0.99.001'
    };
}));
