
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_ARGS 100
#define MAX_COMMANDS 20

// Structure for each command
typedef struct {
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int append;
} Command;

// Built-in cd
void change_directory(char *path) {

    if (path == NULL || strcmp(path, "~") == 0) {
        path = getenv("HOME");
    }

    if (path == NULL || chdir(path) != 0) {
        perror("cd failed");
    }
}

// Built-in history
void show_history() {

    HIST_ENTRY **list = history_list();

    if (list == NULL) {
        printf("No command history found.\n");
        return;
    }

    for (int i = 0; list[i] != NULL; i++) {
        printf("%d  %s\n", i + history_base, list[i]->line);
    }
}

// Parse one command and its redirections

int parse_command(char *text, Command *cmd) {

    int i = 0;
    char *p = text;

    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append = 0;

    while (*p != '\0' && i < MAX_ARGS - 1) {

        // Skip spaces and tabs
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0')
            break;

        // Input redirection
        if (*p == '<') {
            p++;

            while (*p == ' ' || *p == '\t')
                p++;

            cmd->input_file = p;

            while (*p != '\0' && *p != ' ' &&
                   *p != '\t' && *p != '>' && *p != '<')
                p++;

            if (p == cmd->input_file) {
                fprintf(stderr, "Missing input filename\n");
                return -1;
            }

            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }

        // Output redirection
        else if (*p == '>') {

            p++;

            cmd->append = 0;

            if (*p == '>') {
                cmd->append = 1;
                p++;
            }

            while (*p == ' ' || *p == '\t')
                p++;

            cmd->output_file = p;

            while (*p != '\0' && *p != ' ' &&
                   *p != '\t' && *p != '>' && *p != '<')
                p++;

            if (p == cmd->output_file) {
                fprintf(stderr, "Missing output filename\n");
                return -1;
            }

            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }

        // Normal command argument
        else {

            char *start = p;
            char *write = p;
            char quote = '\0';

            cmd->args[i++] = start;

            while (*p != '\0') {

                if (quote != '\0') {
                    if (*p == quote) {
                        quote = '\0';
                        p++;
                    } else {
                        *write++ = *p++;
                    }
                }
                else {
                    if (*p == '"' || *p == '\'') {
                        quote = *p++;
                    }
                    else if (*p == ' ' || *p == '\t' ||
                             *p == '<' || *p == '>') {
                        break;
                    }
                    else {
                        *write++ = *p++;
                    }
                }
            }

            if (quote != '\0') {
                fprintf(stderr, "Unclosed quotation mark\n");
                return -1;
            }

            if (*p != '\0') {

                char separator = *p;

                // Preserve redirection operators
                if (write == p && (separator == '<' || separator == '>')) {

                    memmove(p + 1, p, strlen(p) + 1);
                    *p = '\0';
                    p++;

                }
                else {

                    *write = '\0';

                    if (separator == ' ' || separator == '\t')
                        p++;
                }

            }
            else {
                *write = '\0';
            }
            
        }
    }

    cmd->args[i] = NULL;

    return i > 0 ? 0 : -1;
}


// Apply input and output redirection
void apply_redirection(Command *cmd) {

    if (cmd->input_file != NULL) {

        int fd = open(cmd->input_file, O_RDONLY);

        if (fd < 0) {
            perror("Input file error");
            exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2 input");
            close(fd);
            exit(EXIT_FAILURE);
        }

        close(fd);
    }

    if (cmd->output_file != NULL) {

        int flags = O_WRONLY | O_CREAT;

        if (cmd->append) {
            flags |= O_APPEND;
        }
        else {
            flags |= O_TRUNC;
        }

        int fd = open(cmd->output_file, flags, 0644);

        if (fd < 0) {
            perror("Output file error");
            exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2 output");
            close(fd);
            exit(EXIT_FAILURE);
        }

        close(fd);
    }
}

// Execute a pipeline
void execute_pipeline(Command commands[], int count, int background) {

    int previous_read = -1;
    pid_t pids[MAX_COMMANDS];

    for (int i = 0; i < count; i++) {

        int fd[2] = {-1, -1};

        // Create pipe if another command follows
        if (i < count - 1) {

            if (pipe(fd) < 0) {
                perror("pipe failed");

                if (previous_read != -1)
                    close(previous_read);

                return;
            }
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");

            if (fd[0] != -1) close(fd[0]);
            if (fd[1] != -1) close(fd[1]);
            if (previous_read != -1) close(previous_read);

            return;
        }

        if (pid == 0) {

            // Connect previous pipe to standard input
            if (previous_read != -1) {

                if (dup2(previous_read, STDIN_FILENO) < 0) {
                    perror("dup2 pipe input");
                    exit(EXIT_FAILURE);
                }
            }

            // Connect current pipe to standard output
            if (fd[1] != -1) {

                if (dup2(fd[1], STDOUT_FILENO) < 0) {
                    perror("dup2 pipe output");
                    exit(EXIT_FAILURE);
                }
            }

            // Close unused descriptors
            if (previous_read != -1)
                close(previous_read);

            if (fd[0] != -1)
                close(fd[0]);

            if (fd[1] != -1)
                close(fd[1]);

            // Apply explicit file redirections
            apply_redirection(&commands[i]);

            // Execute command
            execvp(commands[i].args[0], commands[i].args);

            perror("Command execution failed");
            exit(EXIT_FAILURE);
        }

        // Parent process
        pids[i] = pid;

        if (previous_read != -1)
            close(previous_read);

        if (fd[1] != -1)
            close(fd[1]);

        previous_read = fd[0];
    }

    // Close remaining read descriptor
    if (previous_read != -1)
        close(previous_read);

    
    // Wait only if the command is running in foreground
    if (!background) {
        for (int i = 0; i < count; i++) {
            waitpid(pids[i], NULL, 0);
        }
    }
    else {
        printf("[Process %d running in background]\n", pids[count - 1]);
    }
    

}

void check_background_processes() {

    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("\n[Process %d completed]\n", pid);
    }
}    


int main() {

    char *input;

    while (1) {

    check_background_processes();

    input = readline("myShell$ ");

        if (input == NULL) {
            printf("\nExiting shell...\n");
            break;
        }

        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        add_history(input);

        // Check whether command should run in background
        int background = 0;

        int len = strlen(input);

        if (len > 0 && input[len - 1] == '&') {
            background = 1;
            input[len - 1] = '\0';
        }

        // Split input into pipeline commands
        Command commands[MAX_COMMANDS];
        int count = 0;
        int error = 0;

        char *saveptr;
        char *part = strtok_r(input, "|", &saveptr);

        while (part != NULL && count < MAX_COMMANDS) {

            if (parse_command(part, &commands[count]) != 0) {
                error = 1;
                break;
            }

            count++;
            part = strtok_r(NULL, "|", &saveptr);
        }

        if (error || count == 0) {
            fprintf(stderr, "Invalid command\n");
            free(input);
            continue;
        }

        // Built-ins are handled directly by parent
        if (count == 1) {

            if (strcmp(commands[0].args[0], "exit") == 0) {
                printf("Exiting shell...\n");
                free(input);
                break;
            }

            if (strcmp(commands[0].args[0], "cd") == 0) {
                change_directory(commands[0].args[1]);
                free(input);
                continue;
            }

            if (strcmp(commands[0].args[0], "history") == 0) {
                show_history();
                free(input);
                continue;
            }
        }

        // Execute single or multiple commands
        execute_pipeline(commands, count, background);

        free(input);
    }

    return 0;
}
