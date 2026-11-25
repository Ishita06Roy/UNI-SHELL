
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    pid_t pid;

    printf("Starting Parent Process...\n");

    pid = fork();

    if (pid < 0) {
        printf("Fork Failed!\n");
    }
    else if (pid == 0) {
        printf("Hello from Child Process!\n");
        printf("Child PID: %d\n", getpid());
    }
    else {
        wait(NULL);
        printf("Child process completed.\n");
        printf("Parent PID: %d\n", getpid());
    }

    return 0;
}
