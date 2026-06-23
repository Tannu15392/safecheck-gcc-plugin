#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void doublefree(void)
{
    char *buf = (char *)malloc(64);
    if (!buf)
        return;

    strcpy(buf, "test data");
    printf("buf = %s\n", buf);
    free(buf);    
    free(buf);          //double free
}

void freewithnull(void)
{
    char *ptr = (char *)malloc(32);
    if (!ptr)
        return;

    strcpy(ptr, "safe");
    printf("ptr = %s\n", ptr);

    free(ptr);
    ptr = NULL;  
}

void singlefree(void)
{
    int *data = (int *)malloc(5 * sizeof(int));
    if (!data)
        return;
    data[0] = 42;
    printf("data[0] = %d\n", data[0]);
    free(data);
}

int main(void)
{
    doublefree();
    freewithnull();
    singlefree();
    return 0;
}