/*
 * test_loops.c
 * Person 2 owns this file.
 *
 * Tests that loop variables are NOT flagged as unused.
 * This is a false-positive test — the plugin must stay silent
 * on all of these patterns.
 *
 * EXPECTED plugin output:  NOTHING (zero warnings)
 *
 * If the plugin warns on 'i', 'j', or 'k' it has a bug.
 */
#include <stdio.h>
void test(void)
{
    int sum = 0;
    for (int i = 0; i < 10; i++)   
        sum += i;
    printf("sum = %d\n", sum);
}

void test1(void)
{
    int matrix[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
    int total = 0;

    for (int i = 0; i < 3; i++)      
        for (int j = 0; j < 3; j++)
            total += matrix[i][j];

    printf("total = %d\n", total);
}

void test2(void)
{
    int k = 0;
    int acc = 0;
    while (k < 5) {     
        acc += k;
        k++;            
    }
    printf("acc = %d\n", acc);
}

void test3(int n)
{
    int limit = n * 2;  
    int val= 0;
    if (val < limit)
        printf("val < limit\n");
}

int main(void)
{
    test();
    test1();
    test2();
    test3(5);
    return 0;
}