/*
 * test_null_deref.c
 * Phase 2b — Null Pointer Dereference Detection
 *
 * EXPECTED plugin output:
 *   error: null pointer dereference: 'buf' may be NULL   (in no_check)
 *   error: null pointer dereference: 'node' may be NULL  (in struct_deref)
 *
 * NOT expected (no warning):
 *   'ptr'  in checked_ok   — user wrote if(!ptr) before use
 *   'data' in safe_pattern — user wrote if(!data) before use
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * BAD: buf from malloc is used immediately without NULL check.
 * If the system is out of memory, malloc returns NULL and
 * buf[0] is a null dereference crash.
 */
void no_check(void)
{
    char *buf = (char *)malloc(64);  /* may return NULL */
    buf[0] = 'A';                    /* deref without check → error */
    buf[1] = '\0';
    printf("buf = %s\n", buf);
    free(buf);
}

/*
 * GOOD: ptr is checked before use.
 * Plugin must produce NO warning here.
 */
void checked_ok(void)
{
    char *ptr = (char *)malloc(64);
    if (!ptr) return;               /* NULL check — safe */
    strcpy(ptr, "hello");
    printf("ptr = %s\n", ptr);
    free(ptr);
}

typedef struct {
    int value;
    int id;
} node_t;

/*
 * BAD: node is dereferenced via -> without a NULL check.
 */
void struct_deref(void)
{
    node_t *node = (node_t *)malloc(sizeof(node_t));
    node->value = 42;               /* ptr->field without check → error */
    node->id    = 1;
    printf("node.value = %d\n", node->value);
    free(node);
}

/*
 * GOOD: data is checked before field access.
 * Plugin must produce NO warning here.
 */
void safe_pattern(void)
{
    node_t *data = (node_t *)malloc(sizeof(node_t));
    if (!data) return;              /* NULL check — safe */
    data->value = 99;
    printf("data.value = %d\n", data->value);
    free(data);
}

int main(void)
{
    no_check();
    checked_ok();
    struct_deref();
    safe_pattern();
    return 0;
}
