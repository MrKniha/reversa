/* REVERSA sample: CTF-style password check used in tutorials.
 * The "secret" is intentionally visible: the exercise is to find it with
 * cross-references and pseudocode. Build: see samples/build.sh. License: MIT. */
#include <stdio.h>
#include <string.h>

static int check_password(const char *input)
{
    char expected[16];
    strcpy(expected, "reversa");
    strcat(expected, "2026");
    return strcmp(input, expected) == 0;
}

static int check_user(const char *user)
{
    if (strcmp(user, "admin") == 0) {
        return 1;
    }
    return strcmp(user, "student") == 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        puts("Usage: login <user> <password>");
        return 2;
    }
    if (!check_user(argv[1])) {
        puts("Unknown user.");
        return 1;
    }
    if (check_password(argv[2])) {
        puts("Access granted. Flag: CTF{xrefs_are_your_friend}");
        return 0;
    }
    puts("Wrong password.");
    return 1;
}
