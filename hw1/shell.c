#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_wait(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "print current working directory"},
    {cmd_cd, "cd", "change working directory"},
    {cmd_wait, "wait", "waits for background processes"},
};

void path_append(char *path, char *name, char *program)
{
  int len_path;
  int len_name;
  //char *res;

  len_path = strlen(path); 
  //len_name = strlen(name);
  strcpy(program, path);
  strcpy(program + len_path, "/");
  strcpy(program + len_path + 1, name);

}

/*searches for program name in PATH env var directories and current directory 
and if it finds an executabl it returns full path, with program name appended 
char *get_path(char *program)*/
void execv_path(char **argv)
{
  char *path;
  char program[PATH_MAX];
  char *nul;
  int err;

  err = 1;
  path = getenv("PATH");
  if (strchr(argv[0], '/') || !path)
  {
    if (execv(argv[0], argv))
      fprintf(stdout, "shell: %s: No such file or directory\n", program); //print nothing extra
  }
  else
  {
    while (path)
    {
      if (!*path)
        path++;
      if((nul = strchr(path, ':')))
        *nul = '\0';
      path_append(path,argv[0], program);
      if (!(err = execv(program, argv)))
        break;
      path = nul;
    }
    if (err == -1)
      fprintf(stdout, "%s: command not found\n", argv[0]); //print nothing extra
  }
  exit(1);
}

int handle_stdio(char *file, char c)
{
  int fd;
  int flags;

  if (!(fd = open(file, O_CREAT | O_RDWR | O_CLOEXEC, 00700)))
  {
    fprintf(stdout, "Open failed\n");
    return -1;
  }
  if (c == '<')
    dup2(fd, STDIN_FILENO);
  else if (c == '>')
    dup2(fd, STDOUT_FILENO);
  return fd;
}

void signal_default(void)
{
  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
}

void signal_ignore(void)
{
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
}

int parse_arguments(struct tokens *tokens, char** argv, int argc)
{
  char *temp;
  int fd;
  int i;
  int save = -1;

  fd = -1;
  if (argc > 0 && !strcmp(tokens_get_token(tokens, argc - 1), "&"))
  {//make background
    argc--;
  }
  for (i = 0; i < argc; i++)
  {
    temp = tokens_get_token(tokens, i);
    if (!(strcmp(temp, "<") && strcmp(temp, ">")))
    {
      if (save > -1)
        exit(-1);
      fd = handle_stdio(tokens_get_token(tokens, i + 1), temp[0]);
      save = i;
    }
    else
      (argv)[i] = temp;
  }
  if (save > -1)
    argc = save;
  (argv)[argc] = NULL;
  return fd;
}

  /* Searches for a program in directories held by PATH env variable
and runs it if found. returns -1 on error*/
  int run_program(struct tokens * tokens, int argc)
  {
    char *argv[argc];
    pid_t pid;
    int fd;
       
    if (argc == 0)
      return 0;
    if (!(pid = fork()))
    {
      pid = getgid();
      setpgid(pid, pid);
      if (argc > 0 && strcmp(tokens_get_token(tokens, argc - 1), "&"))
      {
        tcsetpgrp(0, pid);
        signal_default(); 
      }
      fd = parse_arguments(tokens, argv, argc); //maybe before fork, to close fd and  tcsetpgrp file descriptor
      execv_path(argv);
      

      //if (fd != -1)
      //close(fd); // never actually happens?
    }

    if(argc > 0  && strcmp(tokens_get_token(tokens, argc - 1), "&"))
      waitpid(pid, NULL, WUNTRACED);
    return 0;
  }

  /* Changes working directory for shell */
  int cmd_cd(struct tokens * tokens)
  {
    char *dest_dir;
    char *curr_dir;
    char buff[PATH_MAX];
    dest_dir = tokens_get_token(tokens, 1);

    curr_dir = getcwd(buff, PATH_MAX);
    if (!(tokens_get_length(tokens) > 2) && !chdir(dest_dir))
    //if(!chdir(dest_dir))
    {
      setenv("PWD", dest_dir, 1);
      setenv("OLDPWD", curr_dir, 1);
      return 0;
    }
    else
      fprintf(stderr, "%s: No such file or directory.\n", dest_dir);//print nothing extra
    return 0;
  }

  /* Prints current working directory for shell */
  int cmd_pwd(unused struct tokens * tokens)
  {
    char buff[PATH_MAX];
    tokens_get_token(tokens, 1);
    getcwd(buff, PATH_MAX);
    fprintf(stdout, "%s\n", buff);
    return 0;
  }

  /* Waits for all backgorund processes to finish */
  int cmd_wait(unused struct tokens *tokens)
  {
    int i = 0;
    while(i != -1)
      i = wait(NULL);
    return 0;
  }

  /* Prints a helpful description for the given command */
  int cmd_help(unused struct tokens * tokens)
  {
    for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
      printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);//print nothing extra
    return 1;
  }

  /* Exits this shell */
  int cmd_exit(unused struct tokens * tokens)
  {
    exit(0);
  }

  /* Looks up the built-in command, if it exists. */
  int lookup(char cmd[])
  {
    for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
      if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
        return i;
    return -1;
  }

  /* Intialization procedures for this shell */
  void init_shell()
  {
    /* Our shell is connected to standard input. */
    shell_terminal = STDIN_FILENO;

    /* Check if we are running interactively */
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive)
    {
      /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
      while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
        kill(-shell_pgid, SIGTTIN);

      /* Saves the shell's process id */
      shell_pgid = getpid();

      /* Take control of the terminal */
      tcsetpgrp(shell_terminal, shell_pgid);

      /* Save the current termios to a variable, so it can be restored later. */
      tcgetattr(shell_terminal, &shell_tmodes);
    }
  }

  int main(unused int argc, unused char *argv[])
  {
    signal_ignore();
    init_shell();
    static char line[4096];
    int line_num = 0;
    /* Please only print shell prompts when standard input is not a tty */
    if (shell_is_interactive)
      fprintf(stdout, "%d: ", line_num); //print nothing extra

    while (fgets(line, 4096, stdin))
    {
      /* Split our line into words. */
      struct tokens *tokens = tokenize(line);

      /* Find which built-in function to run. */
      int fundex = lookup(tokens_get_token(tokens, 0));

      if (fundex >= 0)
      {
        cmd_table[fundex].fun(tokens);
      } else {
      /* REPLACE this to run commands as programs. */
      if (run_program(tokens, tokens_get_length(tokens)) < 0)
        fprintf(stdout, "The program was not found\n"); //print nothing extra
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num); //print nothing extra
    /* Clean up memory */
    tokens_destroy(tokens);
  }
  return 0;
}
