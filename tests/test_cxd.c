#include "cxd/cxd.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

static void check(int cond, const char *name) {
    if (!cond) {
        fprintf(stderr, "FAIL  %s\n", name);
        g_fail++;
    } else {
        fprintf(stdout, "PASS  %s\n", name);
    }
}

static void test_basic(void) {
    CxdDocument doc;
    char err[256];
    check(cxd_parse("null", "<t>", NULL, &doc, err, sizeof(err)), "cxd null parse");
    check(doc.root.kind == CXD_NULL, "cxd null kind");
    cxd_free_document(&doc);

    check(cxd_parse("true", "<t>", NULL, &doc, err, sizeof(err)), "cxd true");
    check(doc.root.kind == CXD_BOOL && doc.root.boolean, "cxd true value");
    cxd_free_document(&doc);

    check(cxd_parse("42", "<t>", NULL, &doc, err, sizeof(err)), "cxd int");
    check(doc.root.number == 42, "cxd int value");
    cxd_free_document(&doc);

    check(cxd_parse("\"hello\"", "<t>", NULL, &doc, err, sizeof(err)), "cxd string");
    check(doc.root.kind == CXD_STRING && strcmp(doc.root.string, "hello") == 0, "cxd string value");
    cxd_free_document(&doc);
}

static void test_implicit(void) {
    const char *src =
        "name: \"Acme\",\n"
        "version: 3,\n"
        "env: production,\n";
    CxdDocument doc;
    char err[256];
    check(cxd_parse(src, "<t>", NULL, &doc, err, sizeof(err)), "cxd implicit parse");
    check(doc.root.kind == CXD_OBJECT, "cxd implicit object");
    const CxdValue *name = cxd_get(&doc.root, "name");
    const CxdValue *ver = cxd_get(&doc.root, "version");
    const CxdValue *env = cxd_get(&doc.root, "env");
    check(name && strcmp(name->string, "Acme") == 0, "cxd implicit name");
    check(ver && ver->number == 3, "cxd implicit version");
    check(env && strcmp(env->string, "production") == 0, "cxd implicit bare");
    cxd_free_document(&doc);
}

static void test_type(void) {
    const char *src = "@type:simplex.config\n\nhost: localhost,\n";
    CxdDocument doc;
    char err[256];
    check(cxd_parse(src, "<t>", NULL, &doc, err, sizeof(err)), "cxd type parse");
    check(doc.type && strcmp(doc.type, "simplex.config") == 0, "cxd @type");
    check(cxd_has(&doc.root, "host"), "cxd type + field");
    cxd_free_document(&doc);
}

static void test_line_capture(void) {
    const char *src = "query: SELECT id FROM users,\n";
    CxdDocument doc;
    char err[256];
    check(cxd_parse(src, "<t>", NULL, &doc, err, sizeof(err)), "cxd line capture parse");
    const CxdValue *q = cxd_get(&doc.root, "query");
    check(q && q->is_line_capture, "cxd line capture flag");
    check(q && strcmp(q->string, "SELECT id FROM users") == 0, "cxd line capture text");
    cxd_free_document(&doc);
}

static void test_anchor_clone(void) {
    const char *src =
        "&base = { arr: [1], nested: { value: 7 } },\n"
        "left: *base,\n"
        "right: *base,\n"
        "merged: { ...base, x: 1 },\n";
    CxdDocument doc;
    char err[256];
    check(cxd_parse(src, "<t>", NULL, &doc, err, sizeof(err)), "cxd anchor clone parse");
    CxdValue *left = cxd_get_mut(&doc.root, "left");
    CxdValue *right = cxd_get_mut(&doc.root, "right");
    const CxdValue *merged = cxd_get(&doc.root, "merged");
    check(left && right && merged, "cxd anchor clone fields");
    CxdValue *left_arr = left ? cxd_get_mut(left, "arr") : NULL;
    CxdValue *right_arr = right ? cxd_get_mut(right, "arr") : NULL;
    check(left_arr && right_arr && left_arr->kind == CXD_ARRAY && right_arr->kind == CXD_ARRAY,
          "cxd anchor clone arrays");
    if (left_arr && left_arr->array_len > 0) left_arr->array[0].number = 99;
    check(right_arr && right_arr->array_len > 0 && right_arr->array[0].number == 1,
          "cxd anchor deep clone independence");
    check(cxd_has(merged, "nested") && cxd_has(merged, "x"), "cxd spread clone + override");
    cxd_free_document(&doc);
}

static void test_semver_bare(void) {
    const char *src =
        "@type:archetype\n"
        "\n"
        "manifestName: demo,\n"
        "manifestVersion: 0.1.0,\n"
        "cxdBuildVersion: \">=1.0.0\",\n"
        "manifestType: build,\n";
    CxdDocument doc;
    char err[256];
    check(cxd_parse(src, "<t>", NULL, &doc, err, sizeof(err)), "cxd semver bare parse");
    check(doc.type && strcmp(doc.type, "archetype") == 0, "cxd semver @type");
    const CxdValue *ver = cxd_get(&doc.root, "manifestVersion");
    check(ver && ver->kind == CXD_STRING && strcmp(ver->string, "0.1.0") == 0, "cxd semver bare string");
    const CxdValue *name = cxd_get(&doc.root, "manifestName");
    check(name && strcmp(name->string, "demo") == 0, "cxd semver name");
    cxd_free_document(&doc);
}

int main(void) {
    test_basic();
    test_implicit();
    test_type();
    test_line_capture();
    test_anchor_clone();
    test_semver_bare();
    if (g_fail) {
        fprintf(stderr, "[cxd-test] %d failed\n", g_fail);
        return 1;
    }
    fprintf(stdout, "[cxd-test] all passed\n");
    return 0;
}
