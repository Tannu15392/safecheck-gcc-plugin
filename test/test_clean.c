#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    int   score;
} student;

student *create(const char *name, int score)
{
    student *s = (student *)malloc(sizeof(student));
    if (!s)
        return NULL;
    s->name = (char *)malloc(strlen(name) + 1);
    if (!s->name) {
        free(s);
        return NULL;
    }
    strcpy(s->name, name);
    s->score = score;
    return s;
}

void print(const student *s)
{
    if (!s)
        return;
    printf("Name: %s  Score: %d\n", s->name, s->score);
}

void freestudent(student*s)
{
    if (!s)
        return;
    free(s->name);   
    free(s);        
}

int sumarr(int *arr, int n)
{
    int total = 0;          
    for (int i = 0; i < n; i++)  
        total += arr[i];
    return total;
}

int main(void)
{
    student *a = create("Ram", 95);
    student *b   = create("Pawan",   82);

    print(a);
    print(b);

    int score[] = {95, 82, 77, 90};
    int n = (int)(sizeof(score) / sizeof(score[0]));
    int total = sumarr(score, n);
    printf("Total array sum: %d\n", total);

    freestudent(a);
    freestudent(b);

    return 0;
}