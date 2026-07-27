#pragma once
/* CXD — Component Extensible Data (portable C library). */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CxdKind {
    CXD_NULL = 0,
    CXD_BOOL,
    CXD_NUMBER,
    CXD_STRING,
    CXD_ARRAY,
    CXD_OBJECT
} CxdKind;

typedef struct CxdValue CxdValue;
typedef struct CxdField CxdField;

struct CxdValue {
    CxdKind kind;
    int line;
    int col;
    bool is_line_capture;
    bool boolean;
    double number;
    char *string;
    CxdValue *array;
    size_t array_len;
    CxdField *object;
    size_t object_len;
};

struct CxdField {
    char *key;
    CxdValue value;
};

typedef struct CxdParseOptions {
    bool allow_type_header;
} CxdParseOptions;

typedef struct CxdDocument {
    char *type;
    CxdValue root;
} CxdDocument;

void cxd_options_default(CxdParseOptions *opts);

bool cxd_parse(const char *source, const char *filename, const CxdParseOptions *opts,
               CxdDocument *out, char *err, size_t err_cap);
bool cxd_parse_file(const char *path, const CxdParseOptions *opts,
                    CxdDocument *out, char *err, size_t err_cap);

void cxd_free_document(CxdDocument *doc);
void cxd_free_value(CxdValue *v);

const CxdValue *cxd_get(const CxdValue *obj, const char *key);
CxdValue *cxd_get_mut(CxdValue *obj, const char *key);
bool cxd_has(const CxdValue *obj, const char *key);

bool cxd_to_json(const CxdValue *v, char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif
