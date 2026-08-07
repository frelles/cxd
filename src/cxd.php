<?php
/**
 * CXD Parser — Component Extensible Data
 * Spec version: 0.99.001
 *
 * Produces a JSON-compatible data model from CXD source text.
 *
 * Usage:
 *   $result = CXD::parse($source, $options);
 *   // $result['data'] — the parsed value
 *   // $result['type'] — @type declaration or null
 *
 * Options:
 *   'keyValidator'  => 'standard' | 'ecmascript' | callable($key): bool
 *   'propertyKeys'  => true | false (default false)
 */

class CXD {

    const VERSION = '0.99.001';

    private $src;
    private $len;
    private $pos;
    private $anchors;
    private $docType;
    private $validateKey;
    private $propertyKeys;

    // ── Character helpers ────────────────────────────────────────

    private static function isDigit($c) { return $c >= '0' && $c <= '9'; }
    private static function isHexDigit($c) { return self::isDigit($c) || ($c >= 'a' && $c <= 'f') || ($c >= 'A' && $c <= 'F'); }
    private static function isWhitespace($c) { return $c === ' ' || $c === "\t" || $c === "\r" || $c === "\n"; }
    private static function isNewline($c) { return $c === "\n" || $c === "\r"; }
    private static function isKeyStart($c) { return ($c >= 'a' && $c <= 'z') || ($c >= 'A' && $c <= 'Z') || $c === '_' || $c === '$'; }
    private static function isKeyChar($c) { return self::isKeyStart($c) || self::isDigit($c) || $c === '-'; }

    // ── Key validators ───────────────────────────────────────────

    private static function standardValidator($k) {
        if (strlen($k) === 0 || !self::isKeyStart($k[0])) return false;
        for ($i = 1; $i < strlen($k); $i++) if (!self::isKeyChar($k[$i])) return false;
        return true;
    }

    private static function ecmascriptValidator($k) {
        if (strlen($k) === 0 || !self::isKeyStart($k[0])) return false;
        for ($i = 1; $i < strlen($k); $i++) {
            $c = $k[$i];
            if (!self::isKeyStart($c) && !self::isDigit($c)) return false;
        }
        return true;
    }

    // ── Error ────────────────────────────────────────────────────

    private function error($msg) {
        $line = 1; $col = 1;
        for ($i = 0; $i < $this->pos && $i < $this->len; $i++) {
            if ($this->src[$i] === "\n") { $line++; $col = 1; } else $col++;
        }
        throw new \RuntimeException("CXD: $msg at line $line, col $col");
    }

    // ── Helpers ──────────────────────────────────────────────────

    private function peek() { return $this->pos < $this->len ? $this->src[$this->pos] : ''; }
    private function at($offset) { return ($this->pos + $offset) < $this->len ? $this->src[$this->pos + $offset] : ''; }
    private function advance() { return $this->src[$this->pos++]; }
    private function expect($ch) {
        if ($this->pos >= $this->len || $this->src[$this->pos] !== $ch)
            $this->error('Expected "' . $ch . '" but got "' . ($this->pos < $this->len ? $this->src[$this->pos] : 'EOF') . '"');
        $this->pos++;
    }

    private function skipWS($preserveComments = false) {
        while ($this->pos < $this->len) {
            $c = $this->src[$this->pos];
            if (self::isWhitespace($c)) { $this->pos++; continue; }
            if (!$preserveComments && $c === '#') {
                while ($this->pos < $this->len && $this->src[$this->pos] !== "\n") $this->pos++;
                continue;
            }
            break;
        }
    }

    // ── @type declaration ────────────────────────────────────────

    private function parseTypeDecl() {
        $saved = $this->pos;
        $this->skipWS();
        if ($this->pos + 6 <= $this->len && substr($this->src, $this->pos, 6) === '@type:') {
            $this->pos += 6;
            $id = '';
            while ($this->pos < $this->len && !self::isNewline($this->src[$this->pos])) {
                $id .= $this->src[$this->pos++];
            }
            $this->docType = trim($id);
            return;
        }
        $this->pos = $saved;
    }

    // ── Numbers ──────────────────────────────────────────────────

    private function parseNumber() {
        $start = $this->pos;
        $negative = false;

        if ($this->src[$this->pos] === '-') { $negative = true; $this->pos++; }

        // Extended literals
        if ($this->pos + 1 < $this->len && $this->src[$this->pos] === '0') {
            $prefix = $this->src[$this->pos + 1];
            if ($prefix === 'x' || $prefix === 'X') {
                $this->pos += 2;
                $hex = '';
                while ($this->pos < $this->len && (self::isHexDigit($this->src[$this->pos]) || $this->src[$this->pos] === '_')) {
                    if ($this->src[$this->pos] !== '_') $hex .= $this->src[$this->pos];
                    $this->pos++;
                }
                if (!strlen($hex)) $this->error('Empty hex literal');
                return ($negative ? -1 : 1) * intval($hex, 16);
            }
            if ($prefix === 'b' || $prefix === 'B') {
                $this->pos += 2;
                $bin = '';
                while ($this->pos < $this->len && ($this->src[$this->pos] === '0' || $this->src[$this->pos] === '1' || $this->src[$this->pos] === '_')) {
                    if ($this->src[$this->pos] !== '_') $bin .= $this->src[$this->pos];
                    $this->pos++;
                }
                if (!strlen($bin)) $this->error('Empty binary literal');
                return ($negative ? -1 : 1) * intval($bin, 2);
            }
            if ($prefix === 'o' || $prefix === 'O') {
                $this->pos += 2;
                $oct = '';
                while ($this->pos < $this->len && (($this->src[$this->pos] >= '0' && $this->src[$this->pos] <= '7') || $this->src[$this->pos] === '_')) {
                    if ($this->src[$this->pos] !== '_') $oct .= $this->src[$this->pos];
                    $this->pos++;
                }
                if (!strlen($oct)) $this->error('Empty octal literal');
                return ($negative ? -1 : 1) * intval($oct, 8);
            }
        }

        // Decimal with space grouping
        $numStr = '';

        // Integer part
        while ($this->pos < $this->len) {
            if (self::isDigit($this->src[$this->pos])) {
                $numStr .= $this->src[$this->pos++];
            } elseif ($this->src[$this->pos] === ' ' && $this->pos + 1 < $this->len && self::isDigit($this->src[$this->pos + 1])) {
                $this->pos++;
            } else break;
        }

        // Fractional part
        if ($this->pos < $this->len && $this->src[$this->pos] === '.') {
            $numStr .= '.';
            $this->pos++;
            while ($this->pos < $this->len) {
                if (self::isDigit($this->src[$this->pos])) {
                    $numStr .= $this->src[$this->pos++];
                } elseif ($this->src[$this->pos] === ' ' && $this->pos + 1 < $this->len && self::isDigit($this->src[$this->pos + 1])) {
                    $this->pos++;
                } else break;
            }
        }

        // Exponent
        if ($this->pos < $this->len && ($this->src[$this->pos] === 'e' || $this->src[$this->pos] === 'E')) {
            $numStr .= $this->src[$this->pos++];
            if ($this->pos < $this->len && ($this->src[$this->pos] === '+' || $this->src[$this->pos] === '-')) $numStr .= $this->src[$this->pos++];
            while ($this->pos < $this->len && self::isDigit($this->src[$this->pos])) $numStr .= $this->src[$this->pos++];
        }

        $n = floatval($numStr);
        if (floor($n) == $n && abs($n) < PHP_INT_MAX) $n = intval($n);
        return $negative ? -$n : $n;
    }

    // ── Strings ──────────────────────────────────────────────────

    private function parseEscape() {
        $this->pos++; // skip backslash
        $c = $this->src[$this->pos++];
        switch ($c) {
            case '\\': return '\\';
            case '"': return '"';
            case "'": return "'";
            case 'n': return "\n";
            case 'r': return "\r";
            case 't': return "\t";
            case 'u':
                $hex = substr($this->src, $this->pos, 4);
                $this->pos += 4;
                return mb_chr(intval($hex, 16), 'UTF-8');
            default: return '\\' . $c;
        }
    }

    private function parseQuotedString($quote) {
        $this->pos++; // opening quote
        $result = '';
        while ($this->pos < $this->len) {
            if ($this->src[$this->pos] === '\\') {
                $result .= $this->parseEscape();
            } elseif ($this->src[$this->pos] === $quote) {
                $this->pos++;
                return $result;
            } else {
                $result .= $this->src[$this->pos++];
            }
        }
        $this->error('Unclosed string');
    }

    private function parseTripleString() {
        $this->pos += 3; // skip opening """
        while ($this->pos < $this->len && $this->src[$this->pos] !== "\n") $this->pos++;
        if ($this->pos < $this->len) $this->pos++;

        $lines = [];
        while ($this->pos < $this->len) {
            $lineStart = $this->pos;
            $line = '';
            while ($this->pos < $this->len && $this->src[$this->pos] !== "\n") {
                $line .= $this->src[$this->pos++];
            }
            if (strpos(ltrim($line), '"""') === 0) {
                $this->pos = $lineStart;
                while ($this->pos < $this->len && $this->src[$this->pos] !== '"') $this->pos++;
                $this->pos += 3;
                break;
            }
            $lines[] = $line;
            if ($this->pos < $this->len) $this->pos++;
        }

        // Strip common indent
        $minIndent = PHP_INT_MAX;
        foreach ($lines as $l) {
            if (trim($l) === '') continue;
            $indent = 0;
            for ($i = 0; $i < strlen($l); $i++) {
                if ($l[$i] === ' ') $indent++;
                elseif ($l[$i] === "\t") $indent += 4;
                else break;
            }
            if ($indent < $minIndent) $minIndent = $indent;
        }
        if ($minIndent === PHP_INT_MAX) $minIndent = 0;

        $stripped = array_map(function($l) use ($minIndent) {
            if (trim($l) === '') return '';
            return substr($l, $minIndent);
        }, $lines);

        while (count($stripped) && $stripped[count($stripped) - 1] === '') array_pop($stripped);

        return implode("\n", $stripped);
    }

    // ── Bare string ──────────────────────────────────────────────

    private function parseBareString() {
        $result = '';
        while ($this->pos < $this->len && self::isKeyChar($this->src[$this->pos])) {
            $result .= $this->src[$this->pos++];
        }
        return $result;
    }

    // ── Line capture ─────────────────────────────────────────────

    private function parseLineCapture() {
        $result = '';
        while ($this->pos < $this->len) {
            $c = $this->src[$this->pos];
            if ($c === "\n" || $c === "\r") break;
            if ($c === ',') break;
            if ($c === '#') {
                $trimmed = rtrim($result);
                if (strlen($trimmed) > 0 && (substr($trimmed, -1) === ',' || substr($result, -1) === ' ' || substr($result, -1) === "\t")) {
                    break;
                }
                $result .= $this->src[$this->pos++];
                continue;
            }
            $result .= $this->src[$this->pos++];
        }
        return trim($result);
    }

    // ── Deep clone ───────────────────────────────────────────────

    private static function deepClone($v) {
        if (!is_array($v)) return $v;
        $o = [];
        foreach ($v as $k => $val) $o[$k] = self::deepClone($val);
        return $o;
    }

    // ── Key parsing ──────────────────────────────────────────────

    private function tryParseKey() {
        $this->skipWS();
        if ($this->pos >= $this->len) return null;

        $propPrefix = false;
        if ($this->propertyKeys && $this->src[$this->pos] === '@' && $this->pos + 1 < $this->len && self::isKeyStart($this->src[$this->pos + 1])) {
            $propPrefix = true;
            $this->pos++;
        }

        $key = null;
        if ($this->src[$this->pos] === '"' || $this->src[$this->pos] === "'") {
            $key = $this->parseQuotedString($this->src[$this->pos]);
        } elseif (self::isKeyStart($this->src[$this->pos])) {
            $start = $this->pos;
            while ($this->pos < $this->len && self::isKeyChar($this->src[$this->pos])) $this->pos++;
            $key = substr($this->src, $start, $this->pos - $start);
            if (!call_user_func($this->validateKey, $key)) {
                $this->pos = $start;
                if ($propPrefix) $this->pos--;
                return null;
            }
        } else {
            if ($propPrefix) $this->pos--;
            return null;
        }

        if ($propPrefix) $key = '@' . $key;
        return $key;
    }

    // ── Value dispatch ───────────────────────────────────────────

    private function parseValue($inStatement = false) {
        $this->skipWS();
        if ($this->pos >= $this->len) $this->error('Unexpected end of input');
        $c = $this->src[$this->pos];

        if ($c === '"' && $this->pos + 2 < $this->len && $this->src[$this->pos + 1] === '"' && $this->src[$this->pos + 2] === '"')
            return $this->parseTripleString();
        if ($c === '"' || $c === "'") return $this->parseQuotedString($c);
        if ($c === '{') return $this->parseObject();
        if ($c === '[') return $this->parseArray();
        if ($c === '@' && $this->at(1) === '[') return $this->parseDescribedList();
        if ($c === '*') return $this->parseAnchorRef();
        if (self::isDigit($c) || ($c === '-' && $this->pos + 1 < $this->len && self::isDigit($this->src[$this->pos + 1])))
            return $this->parseNumber();

        if (self::isKeyStart($c)) {
            $saved = $this->pos;
            $word = '';
            while ($this->pos < $this->len && self::isKeyChar($this->src[$this->pos])) $word .= $this->src[$this->pos++];
            if ($word === 'true') return true;
            if ($word === 'false') return false;
            if ($word === 'null') return null;

            $isBare = true;
            for ($i = 0; $i < strlen($word); $i++) if (!self::isKeyChar($word[$i])) { $isBare = false; break; }
            if ($isBare) return $word;
            $this->pos = $saved;
        }

        if ($inStatement) return $this->parseLineCapture();
        $this->error('Unexpected character "' . $c . '"');
    }

    // ── Array ────────────────────────────────────────────────────

    private function parseArray() {
        $this->expect('[');
        $result = [];
        $this->skipWS();
        while ($this->pos < $this->len && $this->src[$this->pos] !== ']') {
            $result[] = $this->parseValue(false);
            $this->skipWS();
            if ($this->pos < $this->len && $this->src[$this->pos] === ',') { $this->pos++; $this->skipWS(); }
        }
        $this->expect(']');
        return $result;
    }

    // ── Described list ───────────────────────────────────────────

    private function parseDescribedList() {
        $this->pos++; // skip @
        $this->expect('[');
        $fields = [];
        $this->skipWS();
        while ($this->pos < $this->len && $this->src[$this->pos] !== ']') {
            $key = $this->tryParseKey();
            if ($key === null) $this->error('Expected field name in described list');
            $fields[] = $key;
            $this->skipWS();
            if ($this->pos < $this->len && $this->src[$this->pos] === ',') { $this->pos++; $this->skipWS(); }
        }
        $this->expect(']');
        $this->skipWS();

        $rows = $this->parseArray();
        $result = [];
        foreach ($rows as $row) {
            if (!is_array($row)) $this->error('Described list rows must be arrays');
            if (count($row) > count($fields)) $this->error('Described list row has more values than fields');
            $obj = new \stdClass();
            foreach ($fields as $i => $f) {
                $obj->$f = isset($row[$i]) ? $row[$i] : null;
            }
            // Convert to associative array for consistency
            $result[] = (array)$obj;
        }
        return $result;
    }

    // ── Object ───────────────────────────────────────────────────

    private function parseObject() {
        $this->expect('{');
        $obj = $this->parseBody();
        $this->skipWS();
        $this->expect('}');
        return $obj;
    }

    private function parseBody() {
        $obj = [];
        $this->skipWS();
        while ($this->pos < $this->len) {
            $this->skipWS();
            if ($this->pos >= $this->len) break;
            $c = $this->src[$this->pos];

            if ($c === '}') break;

            // Anchor
            if ($c === '&') {
                $this->pos++;
                $name = $this->parseBareString();
                if (!strlen($name)) $this->error('Expected anchor name');
                $this->skipWS();
                if ($this->pos < $this->len && $this->src[$this->pos] === '=') $this->pos++;
                $this->skipWS();
                $this->anchors[$name] = $this->parseValue(false);
                $this->skipWS();
                if ($this->pos < $this->len && $this->src[$this->pos] === ',') $this->pos++;
                continue;
            }

            // Spread
            if ($c === '.' && $this->at(1) === '.' && $this->at(2) === '.') {
                $this->pos += 3;
                $this->skipWS();
                if ($this->pos < $this->len && $this->src[$this->pos] === '*') {
                    $spreadObj = $this->parseAnchorRef();
                } elseif ($this->pos < $this->len && $this->src[$this->pos] === '{') {
                    $spreadObj = $this->parseObject();
                } else {
                    $name = $this->parseBareString();
                    if (!isset($this->anchors[$name])) $this->error('Unknown anchor "' . $name . '"');
                    $spreadObj = self::deepClone($this->anchors[$name]);
                }
                if (!is_array($spreadObj)) $this->error('Spread target must be an object');
                foreach ($spreadObj as $k => $v) $obj[$k] = $v;
                $this->skipWS();
                if ($this->pos < $this->len && $this->src[$this->pos] === ',') $this->pos++;
                continue;
            }

            // Key-value
            $saved = $this->pos;
            $key = $this->tryParseKey();
            if ($key === null) break;
            $this->skipWS();
            if ($this->pos >= $this->len || $this->src[$this->pos] !== ':') {
                $this->pos = $saved;
                break;
            }
            $this->pos++; // skip colon

            $obj[$key] = $this->parseStmtValue();

            $this->skipWS();
            if ($this->pos < $this->len && $this->src[$this->pos] === ',') $this->pos++;
        }
        return $obj;
    }

    // ── Statement value (with line capture fallback) ─────────────

    private function parseStmtValue() {
        $this->skipWS(true);
        if ($this->pos >= $this->len) $this->error('Unexpected end of input after ":"');
        $c = $this->src[$this->pos];

        if ($c === '"' && $this->pos + 2 < $this->len && $this->src[$this->pos + 1] === '"' && $this->src[$this->pos + 2] === '"')
            return $this->parseTripleString();
        if ($c === '"' || $c === "'") return $this->parseQuotedString($c);
        if ($c === '{') return $this->parseObject();
        if ($c === '[') return $this->parseArray();
        if ($c === '@' && $this->at(1) === '[') return $this->parseDescribedList();
        if ($c === '*') return $this->parseAnchorRef();

        if (self::isDigit($c) || ($c === '-' && $this->pos + 1 < $this->len && self::isDigit($this->src[$this->pos + 1]))) {
            $saved = $this->pos;
            $num = $this->parseNumber();
            if ($this->pos < $this->len && !self::isWhitespace($this->src[$this->pos]) && $this->src[$this->pos] !== ',' && $this->src[$this->pos] !== '}' && $this->src[$this->pos] !== ']' && $this->src[$this->pos] !== '#') {
                $this->pos = $saved;
                return $this->parseLineCapture();
            }
            return $num;
        }

        if (self::isKeyStart($c)) {
            $saved = $this->pos;
            $word = '';
            while ($this->pos < $this->len && self::isKeyChar($this->src[$this->pos])) $word .= $this->src[$this->pos++];
            if ($word === 'true') return true;
            if ($word === 'false') return false;
            if ($word === 'null') return null;

            $this->skipWS();
            $next = $this->pos < $this->len ? $this->src[$this->pos] : '';
            if ($next === ',' || $next === '}' || $next === ']' || $next === '#' || $next === '' || self::isNewline($next) || $this->pos >= $this->len) {
                return $word;
            }
            $this->pos = $saved;
            return $this->parseLineCapture();
        }

        return $this->parseLineCapture();
    }

    // ── Anchor reference ─────────────────────────────────────────

    private function parseAnchorRef() {
        $this->pos++; // skip *
        $name = $this->parseBareString();
        if (!isset($this->anchors[$name])) $this->error('Unknown anchor "' . $name . '"');
        return self::deepClone($this->anchors[$name]);
    }

    // ── Implicit object detection ────────────────────────────────

    private function isImplicitObject() {
        $saved = $this->pos;
        $this->skipWS();
        if ($this->pos >= $this->len) { $this->pos = $saved; return false; }
        $c = $this->src[$this->pos];

        if ($c === '&') { $this->pos = $saved; return true; }
        if ($c === '.' && $this->at(1) === '.' && $this->at(2) === '.') { $this->pos = $saved; return true; }

        if ($this->propertyKeys && $c === '@' && $this->pos + 1 < $this->len && self::isKeyStart($this->src[$this->pos + 1])) {
            $this->pos++;
            while ($this->pos < $this->len && self::isKeyChar($this->src[$this->pos])) $this->pos++;
            $this->skipWS();
            $isKV = $this->pos < $this->len && $this->src[$this->pos] === ':';
            $this->pos = $saved;
            return $isKV;
        }

        if (self::isKeyStart($c)) {
            while ($this->pos < $this->len && self::isKeyChar($this->src[$this->pos])) $this->pos++;
            $this->skipWS();
            $isKV = $this->pos < $this->len && $this->src[$this->pos] === ':';
            $this->pos = $saved;
            return $isKV;
        }
        if ($c === '"' || $c === "'") {
            $q = $c;
            $this->pos++;
            while ($this->pos < $this->len && $this->src[$this->pos] !== $q) {
                if ($this->src[$this->pos] === '\\') $this->pos++;
                $this->pos++;
            }
            if ($this->pos < $this->len) $this->pos++;
            $this->skipWS();
            $isKV = $this->pos < $this->len && $this->src[$this->pos] === ':';
            $this->pos = $saved;
            return $isKV;
        }

        $this->pos = $saved;
        return false;
    }

    // ── Entry point ──────────────────────────────────────────────

    public static function parse($source, $options = []) {
        $parser = new self();
        $parser->src = $source;
        $parser->len = strlen($source);
        $parser->pos = 0;
        $parser->anchors = [];
        $parser->docType = null;
        $parser->propertyKeys = !empty($options['propertyKeys']);

        if (isset($options['keyValidator']) && is_callable($options['keyValidator'])) {
            $parser->validateKey = $options['keyValidator'];
        } elseif (isset($options['keyValidator']) && $options['keyValidator'] === 'ecmascript') {
            $parser->validateKey = [__CLASS__, 'ecmascriptValidator'];
        } else {
            $parser->validateKey = [__CLASS__, 'standardValidator'];
        }

        $parser->parseTypeDecl();

        $parser->skipWS();
        if ($parser->pos >= $parser->len) {
            $data = [];
        } elseif ($parser->isImplicitObject()) {
            $data = $parser->parseBody();
        } else {
            $data = $parser->parseValue(false);
        }

        return ['data' => $data, 'type' => $parser->docType];
    }

    /**
     * Parse a CXD file and return the data.
     */
    public static function parseFile($path, $options = []) {
        if (!file_exists($path)) throw new \RuntimeException("CXD: File not found: $path");
        return self::parse(file_get_contents($path), $options);
    }
}
