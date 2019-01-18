#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
    int i, min, sec, remain;
    int total = 20;

    for (i = 0; i < total; i++)
    {
        remain = total - i;
        printf("%d:%d\n", remain / 60, remain % 60);
        sleep(1);
    }
    return 0;
}
