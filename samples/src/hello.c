/* REVERSA sample: small program with a loop, a branch and string output.
 * Build: see samples/build.sh. License: MIT (same as REVERSA). */
#include <stdio.h>

static const char *greetings[] = {"Ahoj", "Hello", "Hallo"};

int sum_to(int n)
{
    int total = 0;
    for (int i = 1; i <= n; i++) {
        total += i;
    }
    return total;
}

int classify(int value)
{
    if (value < 0) {
        return -1;
    } else if (value == 0) {
        return 0;
    }
    return 1;
}

void greet(int count)
{
    for (int i = 0; i < count; i++) {
        printf("%s, world #%d\n", greetings[i % 3], i);
    }
}

int main(int argc, char **argv)
{
    int n = argc * 3;
    greet(argc);
    printf("sum_to(%d) = %d\n", n, sum_to(n));
    printf("classify = %d\n", classify(argc - 2));
    puts("Done.");
    return 0;
}
