#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int compute(int a, int b)
{
    int x   = a * 100;       // unused warning
    int result= a + b;    
    return result;
}

void leakcheck(void)
{
    char *a = (char *)malloc(64);
    if (!a) return;
    strcpy(a, "key=value\nmode=debug\n");
    printf("a: %s\n", a);
    // forgot to free(a)
}

void doublefree(void)
{
    char *buf = (char *)malloc(64);
    if (!buf) return;
    strcpy(buf, "data");
    printf("buf = %s\n", buf);
    free(buf);   // first free
    free(buf);   // double free — error
}

int main(void)
{
    printf("Result: %d\n", compute(3, 4));
    leakcheck();
    doublefree();
    int *arr = malloc(10 * sizeof(int));
    int x = 5; 
    printf("%d", x);
    return 0;
}