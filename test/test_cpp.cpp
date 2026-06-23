#include <iostream>
#include <cstdlib>
#include <cstring>
using namespace std;
int operation(int a, int b)
{
    int mul= a * b;  //unused 
    return a + b;
}

void leakcheck(void)
{
    char *buf = (char *)malloc(128); 
    if (!buf) return;
    strcpy(buf, "hello");
    cout << buf << endl;
    // forgot to free(buf)
}

int main(void)
{
    cout << operation(3, 4) <<endl;
    leakcheck();
    return 0;
}
