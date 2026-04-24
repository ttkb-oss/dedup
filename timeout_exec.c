#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s seconds command [args...]\n", argv[0]);
        return 2;
    }

    int seconds = atoi(argv[1]);
    if (seconds < 0) seconds = 0;
    if (seconds > 0) alarm((unsigned int)seconds);

    // Exec the command provided (argv[2] is the command)
    execvp(argv[2], &argv[2]);
    perror("execvp");
    return 127;
}
