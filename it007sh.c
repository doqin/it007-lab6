#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#define MAX_LINE 80
#define MAX_ARG_LENGTH 80
#define MAX_ARG_NUMBER 80
#define MAX_CMD_NUMBER 10

char *args[MAX_LINE] = {NULL}; /* command line arguments */
struct termios orig;
cc_t erase_char;
pid_t pids[MAX_CMD_NUMBER];
int cmdN = 0;

int parse_argv(char *args, char **argv);
// triple pointer hell lmfao
int parse_cmds(char **argv, char ***cmds);
void remove_newline(char **str);
void set_raw_mode(struct termios *orig, cc_t *erase_char);
void clearline();
void printshell();
int search_for_str(char **arr, char *str);
int strarrlen(char **arr);
int input(int *argn);
int process(int *argn);

void cleanup()
{
    int i = 0;
    while (args[i] != NULL)
    {
        free(args[i]);
        i++;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    exit(0);
}

void signal_handler(int sig)
{
    for (int i = 0; i < cmdN; i++) {
        printf("Sending SIGINT to %d...\n", pids[i]);
        kill(pids[i], SIGINT);
    }
}

/// @brief main function
/// @param 
/// @return exit code
int main(void)
{
    int argn = 0;
    set_raw_mode(&orig, &erase_char);
    signal(SIGINT, signal_handler);
    while (1)
    {
        int res = input(&argn);
        if (res == -1)
            continue;
        if (strcmp(args[argn], "quit") == 0) break;
        res = process(&argn);
        if (res == -1)
            continue;
    }
    cleanup();
    return 0;
}

/// @brief Get command input
/// @param argn number of lines of command lines
/// @return -1 if error, 0 if successful
int input(int *argn)
{
    printshell();
    args[*argn] = (char *)malloc(MAX_ARG_LENGTH);
    if (!args[*argn])
    {
        perror("malloc failed\n");
        return -1;
    }
    args[*argn][0] = '\0';
    int currentline = *argn;
    char buf[3];
    while (1)
    {
        int n = read(STDIN_FILENO, buf, 1);
        if (n <= 0)
            continue;
        if (buf[0] == 0x1b)
        { // ESC
            read(STDIN_FILENO, buf + 1, 2);
            if (buf[1] = '[')
            {
                if (buf[2] == 'A')
                { // Up arrow
                    if (currentline <= 0)
                        continue;
                    clearline();
                    printshell();
                    printf("%s", args[--currentline]);
                    fflush(stdout);
                }
                else if (buf[2] == 'B')
                { // Down arrow
                    if (currentline >= *argn)
                        continue;
                    clearline();
                    printshell();
                    printf("%s", args[++currentline]);
                    fflush(stdout);
                }
            }
        }
        else
        { // Normal input
            if (currentline != *argn)
            { // copy the previous arguments to the current argument
                strcpy(args[*argn], args[currentline]);
                currentline = *argn;
            }
            if (buf[0] == erase_char)
            { // backspace key
                if (strlen(args[*argn]) > 0)
                {
                    args[*argn][strlen(args[*argn]) - 1] = '\0';
                }
                printf("\b \b");
                fflush(stdout);
            }
            else if (buf[0] == '\n')
            { // enter key
                printf("\n");
                fflush(stdout);
                break;
            }
            else
            {
                strncat(args[*argn], &buf[0], 1);
                printf("%c", buf[0]);
                fflush(stdout);
            }
        }
    }
    return 0;
}

/// @brief process command
/// @param argn number of lines of command lines
/// @return -1 if error, 0 if successful
int process(int *argn)
{
    char *argv[MAX_ARG_NUMBER] = {NULL};
    int argvN = parse_argv(args[*argn], argv); // get array of args
    char **cmds[MAX_CMD_NUMBER] = {NULL};
    cmdN = parse_cmds(argv, cmds);
    int prev_read = -1; // read-end FD from previous pipe stage

    for (int i = 0; i < cmdN; i++)
    {
        int p[2] = {-1, -1};
        if (i < cmdN - 1)
        {
            if (pipe(p) == -1)
            {
                fprintf(stderr, "pipe failed: %s\n", strerror(errno));
                return -1;
            }
        }

        // Check for file output operator
        int out = search_for_str(cmds[i], ">");
        int outfd;
        if (out != -1)
        {
            outfd = open(cmds[i][out + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (outfd == -1)
            {
                fprintf(stderr, "open failed: %s\n", strerror(errno));
                return -1;
            }
            cmds[i][out] = NULL;
        }
        // Check for file input operator
        int in = search_for_str(cmds[i], "<");
        int infd;
        if (in != -1)
        {
            infd = open(cmds[i][in + 1], O_RDONLY);
            if (infd == -1)
            {
                fprintf(stderr, "open input.txt failed: %s\n", strerror(errno));
                return -1;
            }
            cmds[i][in] = NULL;
        }

        int pid = fork();
        // fork error
        if (pid < 0)
        {
            fprintf(stderr, "fork failed: %s\n", strerror(errno));
            fflush(stderr);
            if (out != -1)
                close(outfd);
            if (in != -1)
                close(infd);
            return -1;
        }

        if (pid == 0)
        {
            if (prev_read != -1)
            {
                if (dup2(prev_read, STDIN_FILENO) == -1)
                {
                    fprintf(stderr, "dup2 stdin failed: %s\n", strerror(errno));
                    _exit(127);
                }
            }
            if (i < cmdN - 1)
            {
                if (dup2(p[1], STDOUT_FILENO) == -1)
                {
                    fprintf(stderr, "dup2 stdout failed: %s\n", strerror(errno));
                    _exit(127);
                }
            }
            if (prev_read != -1)
                close(prev_read);
            if (i < cmdN - 1)
            {
                close(p[0]);
                close(p[1]);
            }
            if (out != -1)
            {
                if (dup2(outfd, STDOUT_FILENO) == -1)
                {
                    fprintf(stderr, "dup2 stdout failed: %s\n", strerror(errno));
                    _exit(127);
                }
                close(outfd);
            }
            if (in != -1)
            {
                if (dup2(infd, STDIN_FILENO) == -1)
                {
                    fprintf(stderr, "dup2 stdin failed: %s\n", strerror(errno));
                    _exit(127);
                }
                close(infd);
            }
            int err = execvp(cmds[i][0], cmds[i]);
            if (err != 0)
            {
                fprintf(stderr, "An error occurred while executing command: %s\n", strerror(errno));
                fflush(stderr);
                _exit(127);
            }
        }
        else
        {
            if (out != -1)
                close(outfd);
            if (in != -1)
                close(infd);
            pids[i] = pid;
            if (prev_read != -1)
                close(prev_read);
            
            if (i < cmdN - 1) {
                prev_read = p[0];
                close(p[1]);
            }
        }
    }
    int status = 0;
    if (prev_read != -1) close(prev_read);

    // Wait for all children
    for (int i = 0; i < cmdN; ++i) {
        int status = 0;
        if (waitpid(pids[i], &status, 0) == -1) {
            fprintf(stderr, "waitpid %d failed: %s\n", pids[i], strerror(errno));
        }
    }

    *argn = *argn + 1;
    for (int i = 0; argv[i] != NULL; i++)
    {
        free(argv[i]);
    }
    for (int i = 0; cmds[i] != NULL; i++)
    {
        free(cmds[i]);
    }
    return 0;
}

int parse_argv(char *args, char **argv)
{
    int i = 0;
    int argc = 0;
    while (args[i] != '\0')
    {
        while (args[i] == ' ')
            i++;
        if (args[i] == '\0')
            break;
        int start = i++;
        while (args[i] != ' ' && args[i] != '\0')
            i++;
        char arg[MAX_ARG_LENGTH];
        strncpy(arg, args + start, i - start);
        arg[i - start] = '\0';
        argv[argc++] = strdup(arg);
    }
    argv[argc] = NULL;
    return argc;
}

void remove_newline(char **str)
{
    size_t i = 0;
    while (*(*str + i) != '\0' && *(*str + i) != '\n')
        i++;
    if (*(*str + i) == '\n')
        *(*str + i) = '\0';
}

void set_raw_mode(struct termios *orig, cc_t *erase_char)
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, orig);
    raw = *orig;
    *erase_char = raw.c_cc[VERASE];
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_lflag |= ISIG;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

void clearline()
{
    printf("\r\x1b[2K");
    fflush(stdout);
}

void printshell()
{
    printf("it007sh> ");
    fflush(stdout);
}

int search_for_str(char **arr, char *str)
{
    int i = 0;
    while (arr[i] != NULL)
    {
        if (strcmp(arr[i], str) == 0)
            return i;
        i++;
    }
    return -1;
}

int strarrlen(char **arr)
{
    int i = 0;
    while (arr[i] != NULL)
        i++;
    return i;
}

void strarrncpy(char **dest, char **src, size_t n)
{
    int i = 0;
    while (i < n)
    {
        dest[i] = strdup(src[i]);
        i++;
    }
    dest[i] = NULL;
}

int parse_cmds(char **argv, char ***cmds)
{
    int cmds_index = 0;
    int argv_index = 0;
    while (argv[argv_index] != NULL)
    {
        int start = argv_index;
        while (argv[argv_index] != NULL && strcmp(argv[argv_index], "|") != 0)
            argv_index++;
        cmds[cmds_index] = malloc(MAX_ARG_NUMBER / MAX_CMD_NUMBER);
        strarrncpy(cmds[cmds_index], argv + start, argv_index - start);
        cmds_index++;
        if (argv[argv_index] == NULL)
            break;
        if (strcmp(argv[argv_index], "|") == 0)
            argv_index++;
    }
    cmds[cmds_index] = NULL;
    return cmds_index;
}