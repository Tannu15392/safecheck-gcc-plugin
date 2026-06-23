#include <stdio.h>

int add(int a, int b)
{
    int result= a + b;   
    int count = 100;  

    return result;
}

void sum(int *arr, int n)
{
    int sum  = 0; 
    int temp = 99;    

    for (int i = 0; i < n; i++)
        sum += arr[i];

    printf("Sum = %d\n", sum);
}

int main(void)
{
    int data[] = {1, 2, 3, 4, 5};
    printf("3 + 4 = %d\n", add(3, 4));
    sum(data, 5);
    int x;
    printf("%d",x);
    return 0;
}