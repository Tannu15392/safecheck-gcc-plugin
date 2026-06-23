/*
 * test_uninit.c
 * Phase 3 — Variable Used Before Initialization
 *
 * EXPECTED plugin output:
 *   error: variable 'x' is used before initialization   (in use_before_init)
 *   error: variable 'val' is used before initialization (in mixed_bad)
 *
 * NOT expected (no warning):
 *   'result'  — initialized at declaration
 *   'count'   — initialized at declaration
 *   'ptr'     — initialized at declaration via malloc (different phase)
 *   'y'       — assigned before use
 */
#include <stdio.h>
#include <stdlib.h>

/*
 * BAD: x is declared but never assigned before printf uses it.
 * The value of x is garbage — undefined behavior in C.
 */
void use_before_init(void)
{
    int x;                       /* declared, NOT initialized */
    printf("x = %d\n", x);      /* USE before any write → error */
}

/*
 * GOOD: result and count are both initialized at declaration.
 * Plugin must produce NO warning here.
 */
int initialized_ok(int a, int b)
{
    int result = a + b;          /* initialized at declaration */
    int count  = 0;              /* initialized at declaration */
    count += result;
    return count;
}

/*
 * GOOD: y is assigned before it is used.
 * Plugin must produce NO warning here.
 */
void assigned_before_use(void)
{
    int y;
    y = 42;                      /* assigned BEFORE use */
    printf("y = %d\n", y);      /* safe */
}

/*
 * BAD: val is used before initialization inside the if-block.
 * GOOD: total is initialized at declaration.
 */
void mixed_bad(int flag)
{
    int total = 0;               /* initialized — no warning */
    int val;                     /* NOT initialized           */

    if (flag) {
        printf("val = %d\n", val); /* USE before write → error */
    }
    total += 1;
    printf("total = %d\n", total);
}

int main(void)
{
    use_before_init();
    printf("initialized_ok: %d\n", initialized_ok(3, 4));
    assigned_before_use();
    mixed_bad(1);
    return 0;
}
