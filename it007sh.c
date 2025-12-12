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

void parse_args(char *args, char **argv);
void remove_newline(char **str);
void set_raw_mode(struct termios *orig, cc_t *erase_char);
void clearline();
void printshell();
int search_for_str(char **arr, char *str);
int strarrlen(char **arr);

char *args[MAX_LINE] = {NULL}; /* command line arguments */
struct termios orig;
cc_t erase_char;

void cleanup()
{
    int i = 0;
    while (args[i] != NULL)
    {
        free(args[i]);
        i++;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
}

void signal_handler(int sig)
{
    printf("Exiting it007sh...\n");
    fflush(stdout);
    cleanup();
    exit(0);
}

int main(void)
{
    int argn = 0;
    set_raw_mode(&orig, &erase_char);
    signal(SIGINT, signal_handler);
    while (1)
    {
        printshell();
        args[argn] = (char *)malloc(MAX_ARG_LENGTH);
        if (!args[argn])
        {
            perror("malloc failed\n");
            continue;
        }
        args[argn][0] = '\0';
        int currentline = argn;
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
                        if (currentline >= argn)
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
                if (currentline != argn)
                { // copy the previous arguments to the current argument
                    strcpy(args[argn], args[currentline]);
                    currentline = argn;
                }
                if (buf[0] == erase_char)
                { // backspace key
                    if (strlen(args[argn]) > 0)
                    {
                        args[argn][strlen(args[argn]) - 1] = '\0';
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
                    strncat(args[argn], &buf[0], 1);
                    printf("%c", buf[0]);
                    fflush(stdout);
                }
            }
        }
        /*
        if (fgets(args[argn], MAX_ARG_LENGTH, stdin) == NULL) {
          fprintf(stderr, "fgets failed\n");
          free(args[argn]);
          continue;
        }
        */
        // remove_newline(&args[argn]);
        char *argv[MAX_LINE] = {NULL};
        parse_args(args[argn], argv);
        // Check for file output operator
        int out = search_for_str(argv, ">");
        int outfd;
        if (out != -1)
        {
            outfd = open(argv[out + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (outfd == -1)
            {
                fprintf(stderr, "open failed: %s\n", strerror(errno));
                continue;
            }
            argv[out] = NULL;
        }
        // Check for file input operator
        int in = search_for_str(argv, "<");
        int infd;
        if (in != -1)
        {
            infd = open(argv[in + 1], O_RDONLY);
            if (infd == -1)
            {
                fprintf(stderr, "open input.txt failed: %s\n", strerror(errno));
                continue;
            }
            argv[in] = NULL;
        }

        int pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "fork failed: %s\n", strerror(errno));
            fflush(stderr);
            if (out != -1)
                close(outfd);
            if (in != -1)
                close(infd);
            continue;
        }

        if (pid == 0)
        {
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
            int err = execvp(argv[0], argv);
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

            int status = 0;
            if (waitpid(pid, &status, 0) == -1)
            {
                fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
                continue;
            }

            if (WIFEXITED(status))
            {
                int code = WEXITSTATUS(status);
                if (code != 0)
                {
                    fprintf(stderr, "child exited with status %d\n", code);
                }
            }
            else if (WIFSIGNALED(status))
            {
                fprintf(stderr, "child terminated by signal %d\n", WTERMSIG(status));
            }
        }
        argn++;
    }
    cleanup();
    return 0;
}

void parse_args(char *args, char **argv)
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
