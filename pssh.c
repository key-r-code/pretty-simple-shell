#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <limits.h>
#include <signal.h>

#include "builtin.h"
#include "parse.h"
#include "job_control.h"

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

    // Prepare for job creation
    pid_t pids[P->ntasks];
    int num_pids = 0;
    pid_t pgid = 0;
    int is_background = P->background;
    
    // Reconstruct command line from tasks
    char cmdline[PATH_MAX * 2] = {0};
    for (int i = 0; i < P->ntasks; i++) {
        for (int j = 0; P->tasks[i].argv[j] != NULL; j++) {
            if (j > 0 || i > 0) strcat(cmdline, " ");
            strcat(cmdline, P->tasks[i].argv[j]);
        }
        if (i < P->ntasks - 1) strcat(cmdline, " |");
    }
    if (is_background) strcat(cmdline, " &");
    
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
              // Child process
              
              // Set up process group for first process in pipeline
              if (i == 0) {
                   pgid = getpid();
              }
              setpgid(0, pgid);
              
              // Set up pipes
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
              
              // Close all pipe fds in child
              for (int j = 0; j < 2 * num_pipes; j++)
                   close(pipefds[j]);
              
              // Reset signal handlers to default in child
              signal(SIGINT, SIG_DFL);
              signal(SIGQUIT, SIG_DFL);
              signal(SIGTSTP, SIG_DFL);
              signal(SIGTTIN, SIG_DFL);
              signal(SIGTTOU, SIG_DFL);
              signal(SIGCHLD, SIG_DFL);
              
              execvp(P->tasks[i].cmd, P->tasks[i].argv);
              perror(P->tasks[i].cmd);
              exit(EXIT_FAILURE);
         } else {
              // Parent process
              pids[num_pids++] = pid;
              
              // Set up process group for first process
              if (i == 0) {
                   pgid = pid;
              }
              setpgid(pid, pgid);
         }
    }
    
    // Close all pipe fds in parent
    for (int i = 0; i < 2 * num_pipes; i++)
         close(pipefds[i]);
    
    // Create new job
    int job_id = add_job(pids, num_pids, pgid, cmdline, is_background ? BG : FG);
    
    if (job_id < 0) {
         // Failed to create job, kill all processes
         for (int i = 0; i < num_pids; i++) {
              kill(pids[i], SIGKILL);
         }
         return;
    }
    
    Job* job = find_job_by_job_id(job_id);
    
    if (!is_background) {
         // Put job in foreground
         put_job_in_foreground(job, 0);
    } else {
         // Put job in background
         put_job_in_background(job, 0);
    }
    
    set_fg_pgid(getpid());
}

int main(int argc, char **argv)
{
    (void)argc;  
    (void)argv;  
    
    char *cmdline;
    Parse *P;

    init_job_control();

    print_banner();

    while (1) {
        set_fg_pgid(getpid());
        
        cleanup_completed_jobs();
        
        char *prompt = build_prompt();
        cmdline = readline(prompt);
        free(prompt);

        if (!cmdline)       /* EOF */
            exit(EXIT_SUCCESS);

        P = parse_cmdline(cmdline);
        free(cmdline);

        if (!P)
            continue;

        if (P->invalid_syntax) {
            printf("pssh: invalid syntax\n");
            parse_destroy(&P);
            continue;
        }

#if DEBUG_PARSE
        parse_debug(P);
#endif

        execute_tasks(P);

        parse_destroy(&P);
        
        set_fg_pgid(getpid());
    }

    return EXIT_SUCCESS;
}