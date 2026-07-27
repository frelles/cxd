#include "cxd/cxd.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CXD_GROW_CAP(n) (((n) < 8) ? 8 : ((n) * 2))

static void cxd_set_error(char *err, size_t err_cap, const char *msg) {
    if (err && err_cap > 0) snprintf(err, err_cap, "%s", msg);
}

static char *cxd_strdup(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n + 1);
    return d;
}

static bool cxd_read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return false;
    }
    buf[sz] = '\0';
    fclose(f);
    *out = buf;
    return true;
}

void cxd_options_default(CxdParseOptions *opts) {
    opts->allow_type_header = true;
}

void cxd_free_value(CxdValue *v) {
    if (!v) return;
    free(v->string);
    v->string = NULL;
    if (v->array) {
        for (size_t i = 0; i < v->array_len; ++i) cxd_free_value(&v->array[i]);
        free(v->array);
        v->array = NULL;
    }
    if (v->object) {
        for (size_t i = 0; i < v->object_len; ++i) {
            free(v->object[i].key);
            cxd_free_value(&v->object[i].value);
        }
        free(v->object);
        v->object = NULL;
    }
    v->array_len = 0;
    v->object_len = 0;
    v->kind = CXD_NULL;
}

void cxd_free_document(CxdDocument *doc) {
    if (!doc) return;
    free(doc->type);
    doc->type = NULL;
    cxd_free_value(&doc->root);
}

const CxdValue *cxd_get(const CxdValue *obj, const char *key) {
    if (!obj || obj->kind != CXD_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->object_len; ++i)
        if (strcmp(obj->object[i].key, key) == 0) return &obj->object[i].value;
    return NULL;
}

CxdValue *cxd_get_mut(CxdValue *obj, const char *key) {
    if (!obj || obj->kind != CXD_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->object_len; ++i)
        if (strcmp(obj->object[i].key, key) == 0) return &obj->object[i].value;
    return NULL;
}

bool cxd_has(const CxdValue *obj, const char *key) {
    return cxd_get(obj, key) != NULL;
}

static void cxd_obj_set(CxdValue *obj, const char *key, CxdValue val) {
    for (size_t i = 0; i < obj->object_len; ++i) {
        if (strcmp(obj->object[i].key, key) == 0) {
            cxd_free_value(&obj->object[i].value);
            obj->object[i].value = val;
            return;
        }
    }
    CxdField *nf = (CxdField *)realloc(obj->object, (obj->object_len + 1) * sizeof(CxdField));
    if (!nf) return;
    obj->object = nf;
    obj->object[obj->object_len].key = cxd_strdup(key);
    obj->object[obj->object_len].value = val;
    obj->object_len++;
}

static void cxd_arr_push(CxdValue *arr, CxdValue val) {
    size_t new_len = arr->array_len + 1;
    CxdValue *na = (CxdValue *)realloc(arr->array, new_len * sizeof(CxdValue));
    if (!na) return;
    arr->array = na;
    arr->array[arr->array_len++] = val;
}

typedef struct CxdParser {
    const char *src;
    const char *filename;
    CxdParseOptions opts;
    size_t pos;
    int line;
    int col;
    char err[512];
    struct Anchor { char *name; CxdValue value; } *anchors;
    size_t anchor_len;
} CxdParser;

static void cxd_parser_error(CxdParser *p, const char *msg) {
    snprintf(p->err, sizeof(p->err), "%s:%d:%d: %s", p->filename, p->line, p->col, msg);
}

static bool cxd_at_end(const CxdParser *p) { return p->pos >= strlen(p->src); }

static char cxd_peek(const CxdParser *p, size_t off) {
    size_t i = p->pos + off;
    return i < strlen(p->src) ? p->src[i] : '\0';
}

static char cxd_advance(CxdParser *p) {
    char c = p->src[p->pos++];
    if (c == '\n') { p->line++; p->col = 1; } else p->col++;
    return c;
}

static bool cxd_match_str(CxdParser *p, const char *s) {
    size_t len = strlen(s);
    if (strncmp(p->src + p->pos, s, len) != 0) return false;
    for (size_t i = 0; i < len; ++i) cxd_advance(p);
    return true;
}

static void cxd_skip_line(CxdParser *p) {
    while (!cxd_at_end(p) && cxd_peek(p, 0) != '\n') cxd_advance(p);
}

static void cxd_skip_ws(CxdParser *p) {
    while (!cxd_at_end(p)) {
        char c = cxd_peek(p, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') cxd_advance(p);
        else if (c == '#') { cxd_advance(p); cxd_skip_line(p); }
        else break;
    }
}

static void cxd_skip_inline_ws(CxdParser *p) {
    while (!cxd_at_end(p) && (cxd_peek(p, 0) == ' ' || cxd_peek(p, 0) == '\t')) cxd_advance(p);
}

static void cxd_expect(CxdParser *p, char c) {
    cxd_skip_ws(p);
    if (cxd_at_end(p) || cxd_peek(p, 0) != c) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected '%c'", c);
        cxd_parser_error(p, buf);
        return;
    }
    cxd_advance(p);
}

static CxdValue cxd_null_val(void) { CxdValue v; memset(&v, 0, sizeof(v)); return v; }
static CxdValue cxd_bool_val(bool b) { CxdValue v = cxd_null_val(); v.kind = CXD_BOOL; v.boolean = b; return v; }
static CxdValue cxd_num_val(double n) { CxdValue v = cxd_null_val(); v.kind = CXD_NUMBER; v.number = n; return v; }
static CxdValue cxd_str_val(const char *s, bool lc) {
    CxdValue v = cxd_null_val();
    v.kind = CXD_STRING;
    v.string = cxd_strdup(s ? s : "");
    v.is_line_capture = lc;
    return v;
}
static CxdValue cxd_arr_val(void) { CxdValue v = cxd_null_val(); v.kind = CXD_ARRAY; return v; }
static CxdValue cxd_obj_val(void) { CxdValue v = cxd_null_val(); v.kind = CXD_OBJECT; return v; }

static CxdValue cxd_clone_value(const CxdValue *src) {
    CxdValue out = cxd_null_val();
    if (!src) return out;
    out.kind = src->kind;
    out.line = src->line;
    out.col = src->col;
    out.is_line_capture = src->is_line_capture;
    out.boolean = src->boolean;
    out.number = src->number;
    switch (src->kind) {
    case CXD_STRING:
        out.string = cxd_strdup(src->string ? src->string : "");
        break;
    case CXD_ARRAY:
        if (src->array_len > 0) {
            out.array = (CxdValue *)calloc(src->array_len, sizeof(CxdValue));
            if (!out.array) return cxd_null_val();
            out.array_len = src->array_len;
            for (size_t i = 0; i < src->array_len; ++i) out.array[i] = cxd_clone_value(&src->array[i]);
        }
        break;
    case CXD_OBJECT:
        if (src->object_len > 0) {
            out.object = (CxdField *)calloc(src->object_len, sizeof(CxdField));
            if (!out.object) return cxd_null_val();
            out.object_len = src->object_len;
            for (size_t i = 0; i < src->object_len; ++i) {
                out.object[i].key = cxd_strdup(src->object[i].key ? src->object[i].key : "");
                out.object[i].value = cxd_clone_value(&src->object[i].value);
            }
        }
        break;
    default:
        break;
    }
    return out;
}

static bool cxd_match_keyword(CxdParser *p, const char *kw) {
    size_t len = strlen(kw);
    if (strncmp(p->src + p->pos, kw, len) != 0) return false;
    char next = p->pos + len < strlen(p->src) ? p->src[p->pos + len] : '\0';
    if (isalnum((unsigned char)next) || next == '_' || next == '$' || next == '-') return false;
    for (size_t i = 0; i < len; ++i) cxd_advance(p);
    return true;
}

static void cxd_try_type_decl(CxdParser *p, CxdDocument *doc) {
    if (!p->opts.allow_type_header || cxd_at_end(p) || cxd_peek(p, 0) != '@') return;
    size_t save = p->pos; int sl = p->line, sc = p->col;
    cxd_advance(p);
    if (cxd_match_str(p, "type:")) {
        char tid[256];
        size_t n = 0;
        while (!cxd_at_end(p) && cxd_peek(p, 0) != '\n' && cxd_peek(p, 0) != '\r') {
            char c = cxd_peek(p, 0);
            if (c != ' ' && c != '\t' && n + 1 < sizeof(tid)) tid[n++] = cxd_advance(p);
            else if (c == ' ' || c == '\t') cxd_advance(p);
            else break;
        }
        while (n > 0 && isspace((unsigned char)tid[n - 1])) n--;
        tid[n] = '\0';
        if (n == 0) { cxd_parser_error(p, "empty @type: identifier"); return; }
        free(doc->type);
        doc->type = cxd_strdup(tid);
    } else {
        p->pos = save; p->line = sl; p->col = sc;
    }
}

static bool cxd_is_implicit_object(const CxdParser *p) {
    if (cxd_at_end(p)) return false;
    char c = cxd_peek(p, 0);
    if (c == '&') return true;
    if (c == '.' && cxd_peek(p, 1) == '.' && cxd_peek(p, 2) == '.') return true;
    if (!isalpha((unsigned char)c) && c != '_' && c != '$' && c != '"' && c != '\'') return false;
    for (size_t i = p->pos; i < strlen(p->src); ++i) {
        char ch = p->src[i];
        if (ch == ':') return true;
        if (ch == '\n' || ch == '{' || ch == '[') return false;
    }
    return false;
}

static char *cxd_parse_name(CxdParser *p) {
    if (cxd_at_end(p) || (!isalpha((unsigned char)cxd_peek(p, 0)) && cxd_peek(p, 0) != '_' && cxd_peek(p, 0) != '$')) {
        cxd_parser_error(p, "expected identifier");
        return NULL;
    }
    char buf[256];
    size_t n = 0;
    while (!cxd_at_end(p)) {
        char c = cxd_peek(p, 0);
        if (isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-') {
            if (n + 1 < sizeof(buf)) buf[n++] = cxd_advance(p);
            else cxd_advance(p);
        } else break;
    }
    buf[n] = '\0';
    return cxd_strdup(buf);
}

static char *cxd_parse_standard_key(CxdParser *p) {
    if (cxd_at_end(p) || (!isalpha((unsigned char)cxd_peek(p, 0)) && cxd_peek(p, 0) != '_' && cxd_peek(p, 0) != '$')) {
        cxd_parser_error(p, "expected bare key");
        return NULL;
    }
    char buf[512];
    size_t n = 0;
    while (!cxd_at_end(p)) {
        char c = cxd_peek(p, 0);
        if (isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-' || c == '~' || c == '%' || c == '!') {
            if (n + 1 < sizeof(buf)) buf[n++] = cxd_advance(p);
            else cxd_advance(p);
        } else break;
    }
    buf[n] = '\0';
    return cxd_strdup(buf);
}

static void cxd_utf8_encode(uint32_t codepoint, char *out, size_t *n) {
    if (codepoint < 0x80) out[(*n)++] = (char)codepoint;
    else if (codepoint < 0x800) {
        out[(*n)++] = (char)(0xC0 | (codepoint >> 6));
        out[(*n)++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out[(*n)++] = (char)(0xE0 | (codepoint >> 12));
        out[(*n)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (codepoint & 0x3F));
    } else {
        out[(*n)++] = (char)(0xF0 | (codepoint >> 18));
        out[(*n)++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[(*n)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (codepoint & 0x3F));
    }
}

static char *cxd_parse_quoted_string(CxdParser *p) {
    char q = cxd_advance(p);
    char buf[8192];
    size_t n = 0;
    while (!cxd_at_end(p)) {
        char c = cxd_peek(p, 0);
        if (c == q) { cxd_advance(p); break; }
        if (c == '\n' || c == '\r') { cxd_parser_error(p, "unclosed string literal"); return NULL; }
        if (c == '\\') {
            cxd_advance(p);
            char esc = cxd_at_end(p) ? '\0' : cxd_advance(p);
            switch (esc) {
            case '\\': if (n + 1 < sizeof(buf)) buf[n++] = '\\'; break;
            case '"':  if (n + 1 < sizeof(buf)) buf[n++] = '"'; break;
            case '\'': if (n + 1 < sizeof(buf)) buf[n++] = '\''; break;
            case 'n':  if (n + 1 < sizeof(buf)) buf[n++] = '\n'; break;
            case 'r':  if (n + 1 < sizeof(buf)) buf[n++] = '\r'; break;
            case 't':  if (n + 1 < sizeof(buf)) buf[n++] = '\t'; break;
            case 'u': {
                uint32_t codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    if (cxd_at_end(p)) { cxd_parser_error(p, "incomplete \\u escape"); return NULL; }
                    char h = cxd_advance(p);
                    codepoint <<= 4;
                    if (h >= '0' && h <= '9') codepoint |= (uint32_t)(h - '0');
                    else if (h >= 'a' && h <= 'f') codepoint |= (uint32_t)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') codepoint |= (uint32_t)(h - 'A' + 10);
                    else { cxd_parser_error(p, "invalid hex digit in \\u escape"); return NULL; }
                }
                cxd_utf8_encode(codepoint, buf, &n);
                break;
            }
            default:
                if (n + 2 < sizeof(buf)) { buf[n++] = '\\'; buf[n++] = esc; }
                break;
            }
        } else {
            if (n + 1 < sizeof(buf)) buf[n++] = cxd_advance(p);
            else cxd_advance(p);
        }
    }
    buf[n] = '\0';
    return cxd_strdup(buf);
}

static char *cxd_parse_triple_string(CxdParser *p) {
    cxd_advance(p); cxd_advance(p); cxd_advance(p);
    if (!cxd_at_end(p) && cxd_peek(p, 0) == '\r') cxd_advance(p);
    if (!cxd_at_end(p) && cxd_peek(p, 0) == '\n') cxd_advance(p);

    char *lines[64];
    size_t line_count = 0;
    char cur[1024];
    size_t cn = 0;
    cur[0] = '\0';
    while (!cxd_at_end(p)) {
        if (cxd_peek(p, 0) == '"' && cxd_peek(p, 1) == '"' && cxd_peek(p, 2) == '"') {
            lines[line_count++] = cxd_strdup(cur);
            cxd_advance(p); cxd_advance(p); cxd_advance(p);
            break;
        }
        if (cxd_peek(p, 0) == '\r') { cxd_advance(p); continue; }
        if (cxd_peek(p, 0) == '\n') {
            cxd_advance(p);
            lines[line_count++] = cxd_strdup(cur);
            cn = 0; cur[0] = '\0';
            continue;
        }
        if (cn + 1 < sizeof(cur)) cur[cn++] = cxd_advance(p);
        else cxd_advance(p);
    }
    cur[cn] = '\0';
    if (line_count > 0 && lines[line_count - 1][0] == '\0') { free(lines[line_count - 1]); line_count--; }

    size_t common = (size_t)-1;
    for (size_t i = 0; i < line_count; ++i) {
        if (!lines[i][0]) continue;
        size_t lead = 0;
        while (lines[i][lead] == ' ' || lines[i][lead] == '\t') lead++;
        if (common == (size_t)-1 || lead < common) common = lead;
    }
    if (common == (size_t)-1) common = 0;

    char out[8192];
    size_t on = 0;
    for (size_t i = 0; i < line_count; ++i) {
        const char *ln = lines[i];
        size_t len = strlen(ln);
        size_t start = (len >= common) ? common : len;
        for (size_t j = start; j < len && on + 1 < sizeof(out); ++j) out[on++] = ln[j];
        if (i + 1 < line_count && on + 1 < sizeof(out)) out[on++] = '\n';
        free(lines[i]);
    }
    out[on] = '\0';
    return cxd_strdup(out);
}

static bool cxd_is_clean_number(const CxdParser *p) {
    size_t i = p->pos;
    size_t len = strlen(p->src);
    if (i < len && p->src[i] == '-') i++;
    if (i >= len) return false;
    if (p->src[i] == '0' && i + 1 < len) {
        char r = p->src[i + 1];
        if (r == 'x' || r == 'X' || r == 'b' || r == 'B' || r == 'o' || r == 'O') return true;
    }
    int dots = 0;
    while (i < len && (isdigit((unsigned char)p->src[i]) || p->src[i] == '.' || p->src[i] == '_')) {
        if (p->src[i] == '.') {
            dots++;
            /* SemVer / dotted bare-str (0.1.0) is not a CXD number. */
            if (dots > 1) return false;
        }
        i++;
    }
    if (i < len && (p->src[i] == 'e' || p->src[i] == 'E')) {
        i++;
        if (i < len && (p->src[i] == '+' || p->src[i] == '-')) i++;
        while (i < len && isdigit((unsigned char)p->src[i])) i++;
    }
    if (i < len && (isalpha((unsigned char)p->src[i]) || p->src[i] == '_')) return false;
    return true;
}

static double cxd_parse_hex(CxdParser *p) {
    unsigned long long v = 0;
    int any = 0;
    while (!cxd_at_end(p) && (isxdigit((unsigned char)cxd_peek(p, 0)) || cxd_peek(p, 0) == '_')) {
        if (cxd_peek(p, 0) != '_') {
            char c = cxd_advance(p);
            any = 1;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else v |= (unsigned)(c - 'A' + 10);
        } else cxd_advance(p);
    }
    if (!any) cxd_parser_error(p, "empty hex literal");
    return (double)v;
}

static double cxd_parse_bin(CxdParser *p) {
    unsigned long long v = 0;
    int any = 0;
    while (!cxd_at_end(p) && (cxd_peek(p, 0) == '0' || cxd_peek(p, 0) == '1' || cxd_peek(p, 0) == '_')) {
        if (cxd_peek(p, 0) != '_') { v = (v << 1) | (unsigned)(cxd_advance(p) - '0'); any = 1; }
        else cxd_advance(p);
    }
    if (!any) cxd_parser_error(p, "empty binary literal");
    return (double)v;
}

static double cxd_parse_oct(CxdParser *p) {
    unsigned long long v = 0;
    int any = 0;
    while (!cxd_at_end(p) && ((cxd_peek(p, 0) >= '0' && cxd_peek(p, 0) <= '7') || cxd_peek(p, 0) == '_')) {
        if (cxd_peek(p, 0) != '_') { v = (v << 3) | (unsigned)(cxd_advance(p) - '0'); any = 1; }
        else cxd_advance(p);
    }
    if (!any) cxd_parser_error(p, "empty octal literal");
    return (double)v;
}

static double cxd_parse_number(CxdParser *p) {
    bool neg = false;
    if (cxd_peek(p, 0) == '-') { cxd_advance(p); neg = true; }
    if (cxd_peek(p, 0) == '0') {
        char r = cxd_peek(p, 1);
        if (r == 'x' || r == 'X') { cxd_advance(p); cxd_advance(p); double v = cxd_parse_hex(p); return neg ? -v : v; }
        if (r == 'b' || r == 'B') { cxd_advance(p); cxd_advance(p); double v = cxd_parse_bin(p); return neg ? -v : v; }
        if (r == 'o' || r == 'O') { cxd_advance(p); cxd_advance(p); double v = cxd_parse_oct(p); return neg ? -v : v; }
    }
    char num[128];
    size_t n = 0;
    if (!isdigit((unsigned char)cxd_peek(p, 0))) { cxd_parser_error(p, "expected digit"); return 0; }
    num[n++] = cxd_advance(p);
    while (!cxd_at_end(p)) {
        char c = cxd_peek(p, 0);
        if (isdigit((unsigned char)c)) num[n++] = cxd_advance(p);
        else if (c == ' ' && isdigit((unsigned char)cxd_peek(p, 1))) cxd_advance(p);
        else break;
    }
    if (!cxd_at_end(p) && cxd_peek(p, 0) == '.' && isdigit((unsigned char)cxd_peek(p, 1))) {
        num[n++] = cxd_advance(p);
        while (!cxd_at_end(p)) {
            char c = cxd_peek(p, 0);
            if (isdigit((unsigned char)c)) num[n++] = cxd_advance(p);
            else if (c == ' ' && isdigit((unsigned char)cxd_peek(p, 1))) cxd_advance(p);
            else break;
        }
    }
    if (!cxd_at_end(p) && (cxd_peek(p, 0) == 'e' || cxd_peek(p, 0) == 'E')) {
        num[n++] = cxd_advance(p);
        if (!cxd_at_end(p) && (cxd_peek(p, 0) == '+' || cxd_peek(p, 0) == '-')) num[n++] = cxd_advance(p);
        while (!cxd_at_end(p) && isdigit((unsigned char)cxd_peek(p, 0))) num[n++] = cxd_advance(p);
    }
    num[n] = '\0';
    double v = strtod(num, NULL);
    return neg ? -v : v;
}

static char *cxd_try_bare_string(CxdParser *p) {
    size_t start = p->pos;
    size_t i = p->pos;
    size_t len = strlen(p->src);
    while (i < len) {
        char c = p->src[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-') i++;
        else break;
    }
    if (i == start) return NULL;
    size_t j = i;
    while (j < len && (p->src[j] == ' ' || p->src[j] == '\t')) j++;
    char after = j < len ? p->src[j] : '\0';
    if (after == '\n' || after == '\r' || after == ',' || after == '}' || after == ']' || after == '#' || after == '\0') {
        char buf[256];
        size_t n = i - start;
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, p->src + start, n);
        buf[n] = '\0';
        p->pos = i;
        return cxd_strdup(buf);
    }
    return NULL;
}

/* Archetype / SemVer bare-str: 0.1.0, 1.2.3-beta (not a CXD number). */
static char *cxd_try_version_bare_string(CxdParser *p) {
    size_t start = p->pos;
    size_t i = p->pos;
    size_t len = strlen(p->src);
    while (i < len) {
        char c = p->src[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-' || c == '.' || c == '+') i++;
        else break;
    }
    if (i == start) return NULL;
    size_t j = i;
    while (j < len && (p->src[j] == ' ' || p->src[j] == '\t')) j++;
    char after = j < len ? p->src[j] : '\0';
    if (after == '\n' || after == '\r' || after == ',' || after == '}' || after == ']' || after == '#' || after == '\0') {
        char buf[256];
        size_t n = i - start;
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, p->src + start, n);
        buf[n] = '\0';
        p->pos = i;
        return cxd_strdup(buf);
    }
    return NULL;
}

static CxdValue cxd_parse_line_capture(CxdParser *p, int vl, int vc) {
    char buf[4096];
    size_t n = 0;
    while (!cxd_at_end(p)) {
        char c = cxd_peek(p, 0);
        if (c == '\n' || c == '\r' || c == ',') break;
        if (c == '#') {
            bool ws = n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t');
            if (ws) { cxd_advance(p); cxd_skip_line(p); break; }
        }
        if (n + 1 < sizeof(buf)) buf[n++] = cxd_advance(p);
        else cxd_advance(p);
    }
    while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t')) n--;
    buf[n] = '\0';
    CxdValue v = cxd_str_val(buf, true);
    v.line = vl; v.col = vc;
    return v;
}

static CxdValue cxd_parse_array(CxdParser *p, int vl, int vc);
static CxdValue cxd_parse_described(CxdParser *p, int vl, int vc);
static void cxd_parse_body(CxdParser *p, CxdValue *obj, bool implicit);
static CxdValue cxd_parse_value(CxdParser *p, bool in_stmt);

static CxdValue cxd_parse_array(CxdParser *p, int vl, int vc) {
    cxd_advance(p);
    CxdValue arr = cxd_arr_val();
    arr.line = vl; arr.col = vc;
    cxd_skip_ws(p);
    while (!cxd_at_end(p) && cxd_peek(p, 0) != ']') {
        cxd_arr_push(&arr, cxd_parse_value(p, false));
        cxd_skip_ws(p);
        if (!cxd_at_end(p) && cxd_peek(p, 0) == ',') { cxd_advance(p); cxd_skip_ws(p); }
    }
    if (cxd_at_end(p)) { cxd_parser_error(p, "unclosed array"); return arr; }
    cxd_advance(p);
    return arr;
}

static char *cxd_parse_key(CxdParser *p) {
    if (cxd_peek(p, 0) == '"' || cxd_peek(p, 0) == '\'') return cxd_parse_quoted_string(p);
    return cxd_parse_standard_key(p);
}

static CxdValue cxd_parse_described(CxdParser *p, int vl, int vc) {
    cxd_advance(p);
    char *fields[32];
    size_t field_count = 0;
    cxd_skip_ws(p);
    while (!cxd_at_end(p) && cxd_peek(p, 0) != ']') {
        if (field_count < 32) fields[field_count++] = cxd_parse_key(p);
        cxd_skip_ws(p);
        if (!cxd_at_end(p) && cxd_peek(p, 0) == ',') { cxd_advance(p); cxd_skip_ws(p); }
        else break;
    }
    if (cxd_at_end(p)) { cxd_parser_error(p, "unclosed described list header"); return cxd_arr_val(); }
    cxd_advance(p);
    cxd_skip_ws(p);
    if (cxd_at_end(p) || cxd_peek(p, 0) != '[') { cxd_parser_error(p, "expected '[' after described list header"); return cxd_arr_val(); }
    cxd_advance(p);

    CxdValue result = cxd_arr_val();
    result.line = vl; result.col = vc;
    cxd_skip_ws(p);
    while (!cxd_at_end(p) && cxd_peek(p, 0) != ']') {
        if (cxd_peek(p, 0) != '[') { cxd_parser_error(p, "expected inner array in described list"); break; }
        cxd_advance(p);
        CxdValue obj = cxd_obj_val();
        size_t fi = 0;
        cxd_skip_ws(p);
        while (!cxd_at_end(p) && cxd_peek(p, 0) != ']') {
            if (fi >= field_count) { cxd_parser_error(p, "described list row has more values than fields"); break; }
            CxdValue val = cxd_parse_value(p, false);
            cxd_obj_set(&obj, fields[fi++], val);
            cxd_skip_ws(p);
            if (!cxd_at_end(p) && cxd_peek(p, 0) == ',') { cxd_advance(p); cxd_skip_ws(p); }
            else break;
        }
        if (cxd_at_end(p)) { cxd_parser_error(p, "unclosed inner array in described list"); break; }
        cxd_advance(p);
        while (fi < field_count) cxd_obj_set(&obj, fields[fi++], cxd_null_val());
        cxd_arr_push(&result, obj);
        cxd_skip_ws(p);
        if (!cxd_at_end(p) && cxd_peek(p, 0) == ',') { cxd_advance(p); cxd_skip_ws(p); }
        else break;
    }
    if (cxd_at_end(p)) { cxd_parser_error(p, "unclosed described list array"); return result; }
    cxd_advance(p);
    for (size_t i = 0; i < field_count; ++i) free(fields[i]);
    return result;
}

static const CxdValue *cxd_anchor_get(CxdParser *p, const char *name) {
    for (size_t i = 0; i < p->anchor_len; ++i)
        if (strcmp(p->anchors[i].name, name) == 0) return &p->anchors[i].value;
    return NULL;
}

static void cxd_anchor_set(CxdParser *p, const char *name, CxdValue v) {
    for (size_t i = 0; i < p->anchor_len; ++i) {
        if (strcmp(p->anchors[i].name, name) == 0) {
            cxd_free_value(&p->anchors[i].value);
            p->anchors[i].value = v;
            return;
        }
    }
    p->anchors = (struct Anchor *)realloc(p->anchors, (p->anchor_len + 1) * sizeof(*p->anchors));
    p->anchors[p->anchor_len].name = cxd_strdup(name);
    p->anchors[p->anchor_len].value = v;
    p->anchor_len++;
}

static void cxd_parse_body(CxdParser *p, CxdValue *obj, bool implicit) {
    cxd_skip_ws(p);
    while (true) {
        cxd_skip_ws(p);
        if (cxd_at_end(p)) break;
        if (!implicit && cxd_peek(p, 0) == '}') break;

        if (cxd_peek(p, 0) == '&') {
            cxd_advance(p);
            char *name = cxd_parse_name(p);
            cxd_skip_ws(p);
            if (!cxd_at_end(p) && cxd_peek(p, 0) == '=') { cxd_advance(p); cxd_skip_ws(p); }
            CxdValue v = cxd_parse_value(p, true);
            if (name) { cxd_anchor_set(p, name, v); free(name); }
            cxd_skip_ws(p);
            if (!cxd_at_end(p) && cxd_peek(p, 0) == ',') cxd_advance(p);
            continue;
        }

        if (cxd_peek(p, 0) == '.' && cxd_peek(p, 1) == '.' && cxd_peek(p, 2) == '.') {
            cxd_advance(p); cxd_advance(p); cxd_advance(p);
            CxdValue target = cxd_null_val();
            if (!cxd_at_end(p) && cxd_peek(p, 0) == '*') {
                cxd_advance(p);
                char *name = cxd_parse_name(p);
                const CxdValue *a = name ? cxd_anchor_get(p, name) : NULL;
                if (!a) cxd_parser_error(p, "undefined anchor");
                else target = cxd_clone_value(a);
                free(name);
            } else if (!cxd_at_end(p) && cxd_peek(p, 0) == '{') {
                cxd_advance(p);
                target = cxd_obj_val();
                cxd_parse_body(p, &target, false);
                cxd_expect(p, '}');
            } else {
                char *name = cxd_parse_name(p);
                const CxdValue *a = name ? cxd_anchor_get(p, name) : NULL;
                if (!a) cxd_parser_error(p, "undefined anchor");
                else target = cxd_clone_value(a);
                free(name);
            }
            if (target.kind == CXD_OBJECT) {
                for (size_t i = 0; i < target.object_len; ++i)
                    cxd_obj_set(obj, target.object[i].key, cxd_clone_value(&target.object[i].value));
            } else cxd_parser_error(p, "spread of non-object value");
            cxd_free_value(&target);
            cxd_skip_ws(p);
            if (!cxd_at_end(p) && cxd_peek(p, 0) == ',') cxd_advance(p);
            continue;
        }

        char *key = cxd_parse_key(p);
        cxd_skip_inline_ws(p);
        if (cxd_at_end(p) || cxd_peek(p, 0) != ':') {
            cxd_parser_error(p, "expected ':' after key");
            free(key);
            return;
        }
        cxd_advance(p);
        cxd_skip_inline_ws(p);
        CxdValue v;
        if (cxd_at_end(p) || cxd_peek(p, 0) == '\n' || cxd_peek(p, 0) == '\r') {
            cxd_skip_ws(p);
            v = cxd_parse_value(p, false);
        } else {
            v = cxd_parse_value(p, true);
        }
        if (key) { cxd_obj_set(obj, key, v); free(key); }
        cxd_skip_ws(p);
        if (!cxd_at_end(p) && cxd_peek(p, 0) == ',') cxd_advance(p);
    }
}

static CxdValue cxd_parse_value(CxdParser *p, bool in_stmt) {
    int vl = p->line, vc = p->col;
    if (cxd_at_end(p)) return cxd_null_val();

    char c = cxd_peek(p, 0);
    if (c == '{') {
        cxd_advance(p);
        CxdValue v = cxd_obj_val();
        v.line = vl; v.col = vc;
        cxd_parse_body(p, &v, false);
        cxd_expect(p, '}');
        return v;
    }
    if (c == '[') return cxd_parse_array(p, vl, vc);
    if (c == '@') {
        if (cxd_peek(p, 1) == '[') { cxd_advance(p); return cxd_parse_described(p, vl, vc); }
        if (in_stmt) return cxd_parse_line_capture(p, vl, vc);
        cxd_parser_error(p, "unexpected '@'");
        return cxd_null_val();
    }
    if (c == '"' && cxd_peek(p, 1) == '"' && cxd_peek(p, 2) == '"') {
        char *s = cxd_parse_triple_string(p);
        CxdValue v = cxd_str_val(s, false);
        free(s);
        v.line = vl; v.col = vc;
        return v;
    }
    if (c == '"' || c == '\'') {
        char *s = cxd_parse_quoted_string(p);
        CxdValue v = cxd_str_val(s, false);
        free(s);
        v.line = vl; v.col = vc;
        return v;
    }
    if (isdigit((unsigned char)c) || (c == '-' && isdigit((unsigned char)cxd_peek(p, 1)))) {
        if (cxd_is_clean_number(p)) {
            CxdValue v = cxd_num_val(cxd_parse_number(p));
            v.line = vl; v.col = vc;
            return v;
        }
        char *vs = cxd_try_version_bare_string(p);
        if (vs) {
            CxdValue v = cxd_str_val(vs, false);
            free(vs);
            v.line = vl; v.col = vc;
            return v;
        }
        if (in_stmt) return cxd_parse_line_capture(p, vl, vc);
        cxd_parser_error(p, "unexpected character");
        return cxd_null_val();
    }
    if (c == '*') {
        cxd_advance(p);
        char *name = cxd_parse_name(p);
        const CxdValue *a = name ? cxd_anchor_get(p, name) : NULL;
        free(name);
        if (!a) { cxd_parser_error(p, "undefined anchor"); return cxd_null_val(); }
        CxdValue v = cxd_clone_value(a);
        v.line = vl; v.col = vc;
        return v;
    }
    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        if (cxd_match_keyword(p, "true"))  { CxdValue v = cxd_bool_val(true);  v.line = vl; v.col = vc; return v; }
        if (cxd_match_keyword(p, "false")) { CxdValue v = cxd_bool_val(false); v.line = vl; v.col = vc; return v; }
        if (cxd_match_keyword(p, "null"))  { CxdValue v = cxd_null_val(); v.line = vl; v.col = vc; return v; }
        char *bs = cxd_try_bare_string(p);
        if (bs) { CxdValue v = cxd_str_val(bs, false); free(bs); v.line = vl; v.col = vc; return v; }
        if (in_stmt) return cxd_parse_line_capture(p, vl, vc);
        cxd_parser_error(p, "unexpected character");
        return cxd_null_val();
    }
    if (in_stmt) return cxd_parse_line_capture(p, vl, vc);
    cxd_parser_error(p, "unexpected character");
    return cxd_null_val();
}

static bool cxd_parse_internal(CxdParser *p, CxdDocument *doc) {
    memset(doc, 0, sizeof(*doc));
    p->pos = 0; p->line = 1; p->col = 1;
    cxd_skip_ws(p);
    cxd_try_type_decl(p, doc);
    cxd_skip_ws(p);
    if (p->err[0]) return false;
    if (cxd_is_implicit_object(p)) {
        doc->root = cxd_obj_val();
        cxd_parse_body(p, &doc->root, true);
    } else {
        doc->root = cxd_parse_value(p, false);
    }
    if (p->err[0]) return false;
    return true;
}

bool cxd_parse(const char *source, const char *filename, const CxdParseOptions *opts,
               CxdDocument *out, char *err, size_t err_cap) {
    CxdParseOptions def;
    if (!opts) { cxd_options_default(&def); opts = &def; }
    CxdParser p;
    memset(&p, 0, sizeof(p));
    p.src = source ? source : "";
    p.filename = filename ? filename : "<input>";
    p.opts = *opts;
    bool ok = cxd_parse_internal(&p, out);
    if (!ok) cxd_set_error(err, err_cap, p.err);
    for (size_t i = 0; i < p.anchor_len; ++i) {
        free(p.anchors[i].name);
        cxd_free_value(&p.anchors[i].value);
    }
    free(p.anchors);
    return ok;
}

bool cxd_parse_file(const char *path, const CxdParseOptions *opts,
                    CxdDocument *out, char *err, size_t err_cap) {
    char *src = NULL;
    if (!cxd_read_file(path, &src)) {
        cxd_set_error(err, err_cap, "cxd: cannot open file");
        return false;
    }
    bool ok = cxd_parse(src, path, opts, out, err, err_cap);
    free(src);
    return ok;
}

static void cxd_json_escape(const char *s, char *out, size_t cap, size_t *n) {
    for (size_t i = 0; s[i]; ++i) {
        char c = s[i];
        if (*n + 2 >= cap) return;
        if (c == '"' || c == '\\') { out[(*n)++] = '\\'; out[(*n)++] = c; }
        else if (c == '\n') { out[(*n)++] = '\\'; out[(*n)++] = 'n'; }
        else if (c == '\r') { out[(*n)++] = '\\'; out[(*n)++] = 'r'; }
        else if (c == '\t') { out[(*n)++] = '\\'; out[(*n)++] = 't'; }
        else out[(*n)++] = c;
    }
}

static bool cxd_value_to_json(const CxdValue *v, char *buf, size_t cap, size_t *n);

static bool cxd_value_to_json(const CxdValue *v, char *buf, size_t cap, size_t *n) {
    if (*n >= cap) return false;
    switch (v->kind) {
    case CXD_NULL:
        if (*n + 4 >= cap) return false;
        memcpy(buf + *n, "null", 4); *n += 4;
        return true;
    case CXD_BOOL:
        if (v->boolean) {
            if (*n + 4 >= cap) return false;
            memcpy(buf + *n, "true", 4); *n += 4;
        } else {
            if (*n + 5 >= cap) return false;
            memcpy(buf + *n, "false", 5); *n += 5;
        }
        return true;
    case CXD_NUMBER: {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%.17g", v->number);
        size_t len = strlen(tmp);
        if (*n + len >= cap) return false;
        memcpy(buf + *n, tmp, len); *n += len;
        return true;
    }
    case CXD_STRING:
        if (*n + 1 >= cap) return false;
        buf[(*n)++] = '"';
        cxd_json_escape(v->string ? v->string : "", buf, cap, n);
        if (*n + 1 >= cap) return false;
        buf[(*n)++] = '"';
        return true;
    case CXD_ARRAY:
        if (*n + 1 >= cap) return false;
        buf[(*n)++] = '[';
        for (size_t i = 0; i < v->array_len; ++i) {
            if (i > 0) { if (*n + 1 >= cap) return false; buf[(*n)++] = ','; }
            if (!cxd_value_to_json(&v->array[i], buf, cap, n)) return false;
        }
        if (*n + 1 >= cap) return false;
        buf[(*n)++] = ']';
        return true;
    case CXD_OBJECT:
        if (*n + 1 >= cap) return false;
        buf[(*n)++] = '{';
        for (size_t i = 0; i < v->object_len; ++i) {
            if (i > 0) { if (*n + 1 >= cap) return false; buf[(*n)++] = ','; }
            if (*n + 1 >= cap) return false;
            buf[(*n)++] = '"';
            cxd_json_escape(v->object[i].key, buf, cap, n);
            if (*n + 3 >= cap) return false;
            buf[(*n)++] = '"'; buf[(*n)++] = ':'; buf[(*n)++] = ' ';
            if (!cxd_value_to_json(&v->object[i].value, buf, cap, n)) return false;
        }
        if (*n + 1 >= cap) return false;
        buf[(*n)++] = '}';
        return true;
  default:
        return false;
    }
}

bool cxd_to_json(const CxdValue *v, char **out, size_t *out_len) {
    char *buf = (char *)malloc(65536);
    if (!buf) return false;
    size_t n = 0;
    if (!cxd_value_to_json(v, buf, 65536, &n)) { free(buf); return false; }
    buf[n] = '\0';
    *out = buf;
    if (out_len) *out_len = n;
    return true;
}
