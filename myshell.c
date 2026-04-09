#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <ctype.h>

#define MAX_LINE 1024
#define MAX_ARGS 100
#define HISTORY_SIZE 100

char *history[HISTORY_SIZE];
int history_count = 0;

// Add command to history
void add_to_history(const char *cmd) {
    if (history_count < HISTORY_SIZE) {
        history[history_count++] = strdup(cmd);
    } else {
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history[HISTORY_SIZE - 1] = strdup(cmd);
    }
}

// Show command history
void display_history() {
    for (int i = 0; i < history_count; i++) {
        printf("%d: %s\n", i + 1, history[i]);
    }
}

// Signal handler for Ctrl+C
void sigint_handler(int sig) {
    write(STDOUT_FILENO, "\nCaught Ctrl+C (SIGINT). Type 'exit' to quit.\nsh> ", 50);
}

// Strip quotes from string
void strip_quotes(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src != '"') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

// Parse individual command
int parse_command(char *cmd, char **args, char **input_file, char **output_file, int *append, int *background) {
    int arg_count = 0;
    *input_file = NULL;
    *output_file = NULL;
    *append = 0;
    *background = 0;

    char *token = strtok(cmd, " ");
    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " ");
            if (!token) {
                fprintf(stderr, "Error: Expected input file after '<'\n");
                return -1;
            }
            *input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " ");
            if (!token) {
                fprintf(stderr, "Error: Expected output file after '>'\n");
                return -1;
            }
            *output_file = token;
            *append = 0;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " ");
            if (!token) {
                fprintf(stderr, "Error: Expected output file after '>>'\n");
                return -1;
            }
            *output_file = token;
            *append = 1;
        } else if (strcmp(token, "&") == 0) {
            *background = 1;
        } else {
            strip_quotes(token);
            args[arg_count++] = token;
        }
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;
    return arg_count;
}

// Execute external command
int execute_command(char **args, char *input_file, char *output_file, int append, int background) {
    pid_t pid = fork();
    if (pid == 0) {
        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd < 0) {
                perror("Input file error");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (output_file) {
            int fd = open(output_file, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
            if (fd < 0) {
                perror("Output file error");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(args[0], args);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("Fork failed");
        return 1;
    } else {
        if (!background) {
            int status;
            waitpid(pid, &status, 0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        return 0;
    }
}

// Built-in command handler
int handle_builtin(char **args) {
    if (strcmp(args[0], "exit") == 0) {
        exit(0);
    } else if (strcmp(args[0], "cd") == 0) {
        if (!args[1]) {
            fprintf(stderr, "cd: expected argument\n");
        } else {
            if (chdir(args[1]) != 0) {
                perror("cd");
            }
        }
        return 1;
    } else if (strcmp(args[0], "history") == 0) {
        display_history();
        return 1;
    }
    return 0;
}

// Handle pipelining (|) between commands
void handle_pipeline(char *line) {
    char *commands[MAX_ARGS];
    int num_commands = 0;

    char *command = strtok(line, "|");
    while (command) {
        commands[num_commands++] = command;
        command = strtok(NULL, "|");
    }

    int pipefds[2 * (num_commands - 1)];
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < num_commands; i++) {
        char *args[MAX_ARGS];
        char *input_file = NULL, *output_file = NULL;
        int append = 0, background = 0;

        int arg_count = parse_command(commands[i], args, &input_file, &output_file, &append, &background);
        if (arg_count == -1) continue;

        pid_t pid = fork();
        if (pid == 0) {
            if (i > 0) dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            if (i < num_commands - 1) dup2(pipefds[i * 2 + 1], STDOUT_FILENO);

            for (int j = 0; j < 2 * (num_commands - 1); j++) close(pipefds[j]);

            if (execvp(args[0], args) == -1) {
                perror("execvp");
            }
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("Fork failed");
        }
    }

    for (int i = 0; i < 2 * (num_commands - 1); i++) close(pipefds[i]);
    for (int i = 0; i < num_commands; i++) wait(NULL);
}

// Execute multiple commands (split by ; and &&)
void execute_line(char *line) {
    char *cmd_ptr = line;
    char *next_cmd;

    while (cmd_ptr != NULL) {
        next_cmd = strstr(cmd_ptr, "&&");
        int run = 1;
        if (!next_cmd) {
            next_cmd = strstr(cmd_ptr, ";");
            run = 2;
        }

        if (next_cmd) {
            *next_cmd = '\0';
        }

        // Trim
        while (*cmd_ptr == ' ') cmd_ptr++;
        char *end = cmd_ptr + strlen(cmd_ptr) - 1;
        while (end > cmd_ptr && *end == ' ') *end-- = '\0';

        if (strlen(cmd_ptr) > 0) {
            // Add to history
            add_to_history(cmd_ptr);

            if (strchr(cmd_ptr, '|')) {
                handle_pipeline(cmd_ptr);
            } else {
                char *args[MAX_ARGS];
                char *input_file, *output_file;
                int append, background;

                int arg_count = parse_command(cmd_ptr, args, &input_file, &output_file, &append, &background);
                if (arg_count == -1) {
                    if (run == 1) break;
                    cmd_ptr = next_cmd + 2;
                    continue;
                }

                if (arg_count > 0 && handle_builtin(args)) {
                    // Handled built-in
                } else {
                    int status = execute_command(args, input_file, output_file, append, background);
                    if (run == 1 && status != 0) break;
                }
            }
        }

        cmd_ptr = next_cmd ? next_cmd + (run == 1 ? 2 : 1) : NULL;
    }
}

//history
int current_history = -1;

void reset_input_mode(struct termios *orig_termios) {
    tcsetattr(STDIN_FILENO, TCSANOW, orig_termios);
}
void set_input_mode(struct termios *orig_termios) {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, orig_termios);
    new_termios = *orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}




// Read input with support for arrow keys
void read_input(char *line) {
    struct termios orig_termios;
    set_input_mode(&orig_termios);

    int pos = 0;
    int ch;
    current_history = history_count;

    while (1) {
        ch = getchar();

        if (ch == 127 || ch == 8) { // Backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (ch == '\n') {
            line[pos] = '\0';
            printf("\n");
            break;
        } else if (ch == 27) { // Escape sequence
            if (getchar() == '[') {
                ch = getchar();
                if (ch == 'A') { // Up arrow
                    if (current_history > 0) {
                        current_history--;
                        // Clear line
                        printf("\33[2K\rsh> ");
                        strcpy(line, history[current_history]);
                        pos = strlen(line);
                        printf("%s", line);
                        fflush(stdout);
                    }
                } else if (ch == 'B') { // Down arrow
                    if (current_history < history_count - 1) {
                        current_history++;
                        printf("\33[2K\rsh> ");
                        strcpy(line, history[current_history]);
                        pos = strlen(line);
                        printf("%s", line);
                        fflush(stdout);
                    } else if (current_history == history_count - 1) {
                        current_history++;
                        printf("\33[2K\rsh> ");
                        pos = 0;
                        line[0] = '\0';
                        fflush(stdout);
                    }
                }
            }
        } else if (isprint(ch)) {
            if (pos < MAX_LINE - 1) {
                line[pos++] = ch;
                putchar(ch);
                fflush(stdout);
            }
        }
    }

    reset_input_mode(&orig_termios);
}

// Main program loop
int main() {
    char line[MAX_LINE];

    signal(SIGINT, sigint_handler);

    while (1) {
        printf("sh> ");
        fflush(stdout);

        read_input(line);

        // Skip empty lines
        if (strlen(line) == 0) continue;

        execute_line(line);
    }

    return 0;
}


