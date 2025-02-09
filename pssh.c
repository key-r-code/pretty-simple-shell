#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <limits.h>


#include "builtin.h"
#include "parse.h"

/*******************************************
 * Set to 1 to view the command line parse *
 * Set to 0 before submitting!             *
 *******************************************/
#define DEBUG_PARSE 0


void print_banner()
{
    printf ("                    ________   \n");
    printf ("_________________________  /_  \n");
    printf ("___  __ \\_  ___/_  ___/_  __ \\ \n");
    printf ("__  /_/ /(__  )_(__  )_  / / / \n");
    printf ("_  .___//____/ /____/ /_/ /_/  \n");
    printf ("/_/ Type 'exit' or ctrl+c to quit\n\n");
}


/* **returns** a string used to build the prompt
 * (DO NOT JUST printf() IN HERE!)
 *
 * Note:
 *   If you modify this function to return a string on the heap,
 *   be sure to free() it later when appropirate!  */
static char *build_prompt()
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        strcpy(cwd, "?");
    }
    size_t len = strlen(cwd) + 3; 
    char *prompt = malloc(len);
    if (!prompt) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    snprintf(prompt, len, "%s$ ", cwd);
    return prompt;
}

/* return true if command is found, either:
 *   - a valid fully qualified path was supplied to an existing file
 *   - the executable file was found in the system's PATH
 * false is returned otherwise */
static int command_found(const char *cmd)
{
    char *dir;
    char *tmp;
    char *PATH;
    char *state;
    char probe[PATH_MAX];

    int ret = 0;

    if (access(cmd, X_OK) == 0)
        return 1;

    PATH = strdup(getenv("PATH"));

    for (tmp=PATH; ; tmp=NULL) {
        dir = strtok_r(tmp, ":", &state);
        if (!dir)
            break;

        strncpy(probe, dir, PATH_MAX-1);
        strncat(probe, "/", PATH_MAX-1);
        strncat(probe, cmd, PATH_MAX-1);

        if (access(probe, X_OK) == 0) {
            ret = 1;
            break;
        }
    }

    free(PATH);
    return ret;
}


/* Called upon receiving a successful parse.
 * This function is responsible for cycling through the
 * tasks, and forking, executing, etc as necessary to get
 * the job done! */
void execute_tasks(Parse *P)
{
    int stat;

    if (P->ntasks <= 0)
        return;
    
    // single builtin command handler - if it's a builtin, gets executed directly in the parent
    if (P->ntasks == 1 && is_builtin(P->tasks[0].cmd)) {
         if (!strcmp(P->tasks[0].cmd, "which"))
              builtin_which(P->tasks[0]);
         else
              builtin_execute(P->tasks[0]);
         return;
    }
    
    // single non-builtin command without piping
    if (P->ntasks == 1) {
         if (!command_found(P->tasks[0].cmd)) {
              printf("pssh: command not found: %s\n", P->tasks[0].cmd);
              return;
         }
         pid_t pid = fork();
         if (pid < 0) {
              perror("fork");
              exit(EXIT_FAILURE);
         }
         if (pid == 0) {
              if (P->infile) {
                   int fd = open(P->infile, O_RDONLY);
                   if (fd < 0) {
                        perror("open infile");
                        exit(EXIT_FAILURE);
                   }
                   if (dup2(fd, STDIN_FILENO) < 0) {
                        perror("dup2 infile");
                        exit(EXIT_FAILURE);
                   }
                   close(fd);
              }
              if (P->outfile) {
                   int fd = open(P->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                   if (fd < 0) {
                        perror("open outfile");
                        exit(EXIT_FAILURE);
                   }
                   if (dup2(fd, STDOUT_FILENO) < 0) {
                        perror("dup2 outfile");
                        exit(EXIT_FAILURE);
                   }
                   close(fd);
              }
              execvp(P->tasks[0].cmd, P->tasks[0].argv);
              perror(P->tasks[0].cmd);
              exit(EXIT_FAILURE);
         } else {
              waitpid(pid, &stat, 0);
         }
         return;
    }

    // pipeline execution for multiple commands | | | 
    
    int num_tasks = P->ntasks;
    int num_pipes = num_tasks - 1;
    int pipefds[2 * num_pipes];
    for (int i = 0; i < num_pipes; i++) {
         if (pipe(pipefds + i*2) < 0) {
              perror("pipe");
              exit(EXIT_FAILURE);
         }
    }
    
    for (int i = 0; i < num_tasks; i++) {
         if (!is_builtin(P->tasks[i].cmd) && !command_found(P->tasks[i].cmd)) {
              printf("pssh: command not found: %s\n", P->tasks[i].cmd);
              return;
         }
         pid_t pid = fork();
         if (pid < 0) {
              perror("fork");
              exit(EXIT_FAILURE);
         }
         if (pid == 0) {
              if (i == 0) {
                   if (P->infile) {
                        int fd = open(P->infile, O_RDONLY);
                        if (fd < 0) {
                             perror("open infile");
                             exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDIN_FILENO) < 0) {
                             perror("dup2 infile");
                             exit(EXIT_FAILURE);
                        }
                        close(fd);
                   }
              }
              if (i > 0) {
                   if (dup2(pipefds[(i-1)*2], STDIN_FILENO) < 0) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                   }
              }
              if (i == num_tasks - 1) {
                   if (P->outfile) {
                        int fd = open(P->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd < 0) {
                             perror("open outfile");
                             exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDOUT_FILENO) < 0) {
                             perror("dup2 outfile");
                             exit(EXIT_FAILURE);
                        }
                        close(fd);
                   }
              }
              if (i < num_tasks - 1) {
                   if (dup2(pipefds[i*2 + 1], STDOUT_FILENO) < 0) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                   }
              }
              for (int j = 0; j < 2 * num_pipes; j++)
                   close(pipefds[j]);
              
              execvp(P->tasks[i].cmd, P->tasks[i].argv);
              perror(P->tasks[i].cmd);
              exit(EXIT_FAILURE);
         }
    }
    
    for (int i = 0; i < 2 * num_pipes; i++)
         close(pipefds[i]);
    
    for (int i = 0; i < num_tasks; i++)
         wait(&stat);
}

int main(int argc, char **argv)
{
    char *cmdline;
    Parse *P;

    print_banner();

    while (1) {
        char *prompt = build_prompt();
        cmdline = readline(prompt);
        free(prompt);

        if (!cmdline)       
            exit(EXIT_SUCCESS);

        P = parse_cmdline(cmdline);
        if (!P)
            goto next;

        if (P->invalid_syntax) {
            printf("pssh: invalid syntax\n");
            goto next;
        }

#if DEBUG_PARSE
        parse_debug(P);
#endif

        execute_tasks(P);

    next:
        parse_destroy(&P);
        free(cmdline);
    }
}