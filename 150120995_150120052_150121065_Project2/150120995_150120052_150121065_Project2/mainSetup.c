#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_LINE 128
#define MAX_ARGS 32
#define HISTORY_COUNT 10

char history[HISTORY_COUNT][MAX_LINE];
int history_index = 0;
int history_size = 0;
pid_t foreground_pid = -1;

void setup(char inputBuffer[], char *args[], int *background) {
    int length, i, start = -1, ct = 0;
    length = read(STDIN_FILENO, inputBuffer, MAX_LINE);

    if (length == 0) exit(0);
    if ((length < 0) && (errno != EINTR)) {
        perror("Error reading command");
        exit(-1);
    }

    for (i = 0; i < length; i++) {
        switch (inputBuffer[i]) {
            case ' ':
            case '\t':
                if (start != -1) {
                    args[ct++] = &inputBuffer[start];
                    inputBuffer[i] = '\0';
                    start = -1;
                }
                break;
            case '\n':
                if (start != -1) args[ct++] = &inputBuffer[start];
                inputBuffer[i] = '\0';
                args[ct] = NULL;
                break;
            default:
                if (start == -1) start = i;
                if (inputBuffer[i] == '&') {
                    *background = 1;
                    inputBuffer[i] = '\0';
                }
        }
    }
    args[ct] = NULL;
}

void add_to_history(const char *command) {
    strncpy(history[history_index], command, MAX_LINE);
    history[history_index][MAX_LINE - 1] = '\0';
    history_index = (history_index + 1) % HISTORY_COUNT;
    if (history_size < HISTORY_COUNT) {
        history_size++;
    }
}

void print_history() {
    for (int i = 0; i < history_size; i++) {
        int index = (history_index - history_size + i + HISTORY_COUNT) % HISTORY_COUNT;
        printf("%d %s\n", i, history[index]);
    }
}

void execute_history_command(int index, char *args[], int *background) {
    if (index < 0 || index >= history_size) {
        fprintf(stderr, "Invalid history index.\n");
        return;
    }
    int actual_index = (history_index - history_size + index + HISTORY_COUNT) % HISTORY_COUNT;
    char inputBuffer[MAX_LINE];
    strcpy(inputBuffer, history[actual_index]);
    setup(inputBuffer, args, background);
}

void handle_sigint(int sig) {
    if (foreground_pid > 0) {
        kill(foreground_pid, SIGKILL);
        foreground_pid = -1;
        printf("Foreground process terminated.\n");
    }
}

int redirect_io(char *args[]) {
    int input_fd = -1, output_fd = -1, error_fd = -1;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            input_fd = open(args[i + 1], O_RDONLY);
            if (input_fd < 0) {
                perror("Input redirection error");
                return -1;
            }
            dup2(input_fd, STDIN_FILENO);
            args[i] = NULL;
        } else if (strcmp(args[i], ">") == 0) {
            output_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_fd < 0) {
                perror("Output redirection error");
                return -1;
            }
            dup2(output_fd, STDOUT_FILENO);
            args[i] = NULL;
        } else if (strcmp(args[i], ">>") == 0) {
            output_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (output_fd < 0) {
                perror("Append redirection error");
                return -1;
            }
            dup2(output_fd, STDOUT_FILENO);
            args[i] = NULL;
        } else if (strcmp(args[i], "2>") == 0) {
            error_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (error_fd < 0) {
                perror("Error redirection error");
                return -1;
            }
            dup2(error_fd, STDERR_FILENO);
            args[i] = NULL;
        }
    }
    return 0;
}

void execute_command(char *args[], int background) {
    if (strcmp(args[0], "history") == 0) {
        print_history();
        return;
    }

    if (strcmp(args[0], "exit") == 0) {
        if (waitpid(-1, NULL, WNOHANG) == 0) {
            printf("There are still background processes running.\n");
            return;
        }
        exit(0);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        exit(1);
    }

    if (pid == 0) {
        if (redirect_io(args) < 0) {
            exit(1);
        }
        char *path = getenv("PATH");
        char *dir = strtok(path, ":");
        char full_path[MAX_LINE];

        while (dir != NULL) {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
            execv(full_path, args);
            dir = strtok(NULL, ":");
        }

        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    } else {
        if (!background) {
            foreground_pid = pid;
            waitpid(pid, NULL, 0);
            foreground_pid = -1;
        } else {
            printf("Process running in background: %d\n", pid);
        }
    }
}

int main() {
    char inputBuffer[MAX_LINE];
    char *args[MAX_ARGS];
    int background;

    signal(SIGINT, handle_sigint);

    while (1) {
        printf("myshell: ");
        fflush(stdout);

        setup(inputBuffer, args, &background);

        if (args[0] != NULL) {
            add_to_history(inputBuffer);
            execute_command(args, background);
        }
    }

    return 0;
}
