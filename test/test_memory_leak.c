/*
 * test_memory_leak.c
 * Person 2 owns this file.
 *
 * Tests memory leak detection — malloc with no matching free().
 *
 * EXPECTED plugin output:
 *   error: memory leak: 'buf' is allocated but never freed
 *   note:  'buf' was allocated here
 *   suggestion: add free(buf) before all return statements
 *
 *   error: memory leak: 'arr' is allocated but never freed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * BAD: allocates 'buf' but never calls free(buf).
 * The memory is lost when the function returns.
 */
void always_leaks(void)
{
    char *buf = (char *)malloc(128);    /* LEAK — never freed */
    if (!buf)
        return;

    strcpy(buf, "hello safecheck");
    printf("buf = %s\n", buf);

    /* BUG: forgot to call free(buf) here */
}

/*
 * BAD: leaks on the error return path.
 * 'arr' is freed only on the success path.
 */
int leaks_on_error(int n)
{
    int *arr = (int *)malloc(n * sizeof(int));  /* LEAK on early return */
    if (!arr)
        return -1;

    if (n < 1) {
        return -2;      /* BUG: arr not freed here */
    }

    for (int i = 0; i < n; i++)
        arr[i] = i;

    printf("arr[0] = %d\n", arr[0]);
    free(arr);          /* freed only on this path */
    return 0;
}

/*
 * GOOD: properly allocates and frees.
 * Plugin must produce NO warning here.
 */
void no_leak(void)
{
    int *p = (int *)malloc(10 * sizeof(int));
    if (!p)
        return;
    p[0] = 99;
    printf("p[0] = %d\n", p[0]);
    free(p);            /* correctly freed */
}

int main(void)
{
    always_leaks();
    leaks_on_error(5);
    leaks_on_error(0);
    no_leak();
    return 0;
}