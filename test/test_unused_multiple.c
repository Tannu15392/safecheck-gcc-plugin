/*
 * test_unused_multiple.c
 * Person 2 owns this file.
 *
 * Tests that the plugin correctly finds multiple unused variables
 * inside the same function, and also handles multiple functions.
 *
 * EXPECTED plugin output:
 *   warning: unused variable 'x'       (in compute)
 *   warning: unused variable 'debug'   (in compute)
 *   warning: unused variable 'backup'  (in process)
 *
 * NOT expected (no warning):
 *   'result'  — used (returned)
 *   'total'   — used (printed)
 *   'i'       — used (loop counter)
 */
#include <stdio.h>

int compute(int a, int b)
{
    int x      = a * 10;    /* UNUSED — warning expected */
    int debug  = 42;        /* UNUSED — warning expected */
    int result = a + b;     /* USED   — returned */

    return result;
}

void process(int *data, int n)
{
    int total  = 0;         /* USED — printed */
    int backup = -1;        /* UNUSED — warning expected */

    for (int i = 0; i < n; i++)
        total += data[i];

    printf("total = %d\n", total);
}

int main(void)
{
    int arr[] = {10, 20, 30};
    printf("result = %d\n", compute(3, 7));
    process(arr, 3);
    return 0;
}