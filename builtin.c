#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>

#include "builtin.h"
#include "parse.h"
#include "job_control.h"

static char *builtin[] = {
    "exit",   /* exits the shell */
    "which",  /* displays full path to command */
    "jobs",   /* list current jobs */
    "fg",     /* bring job to foreground */
    "bg",     /* continue job in background */
    "kill",   /* send signal to job */
    NULL
};

int is_builtin(char *cmd)
{
    int i;

    for (i=0; builtin[i]; i++) {
        if (!strcmp(cmd, builtin[i]))
            return 1;
    }

    return 0;
}

void builtin_execute(Task T)
{
    if (!strcmp(T.cmd, "exit")) {
        exit(EXIT_SUCCESS);
    } else if (!strcmp(T.cmd, "which")) {
        builtin_which(T);
    } else if (!strcmp(T.cmd, "jobs")) {
        builtin_jobs(T);
    } else if (!strcmp(T.cmd, "fg")) {
        builtin_fg(T);
    } else if (!strcmp(T.cmd, "bg")) {
        builtin_bg(T);
    } else if (!strcmp(T.cmd, "kill")) {
        builtin_kill(T);
    } else {
        printf("pssh: builtin command: %s (not implemented!)\n", T.cmd);
    }
}

int builtin_jobs(Task T) {
    (void)T;  // Mark parameter as intentionally unused
    
    // Print active jobs
    int active_jobs = 0;
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].status != TERM) {
            print_job_status(&jobs[i], 0);
            active_jobs++;
        }
    }
    
    return active_jobs == 0;  // Return success if any jobs were printed
}

static int parse_job_number(const char* arg) {
    if (!arg || arg[0] != '%') {
        return -1;
    }
    
    // Skip the % character
    arg++;
    
    // Check if the rest is a valid number
    for (int i = 0; arg[i]; i++) {
        if (!isdigit(arg[i])) {
            return -1;
        }
    }
    
    return atoi(arg);
}

int builtin_fg(Task T) {
    if (!T.argv[1]) {
        printf("Usage: fg %%<job number>\n");
        return 1;
    }
    
    int job_id = parse_job_number(T.argv[1]);
    if (job_id < 0) {
        printf("pssh: invalid job number: %s\n", T.argv[1]);
        return 1;
    }
    
    Job* job = find_job_by_job_id(job_id);
    if (!job) {
        printf("pssh: invalid job number: %s\n", T.argv[1]);
        return 1;
    }
    
    put_job_in_foreground(job, 1);
    return 0;
}

int builtin_bg(Task T) {
    if (!T.argv[1]) {
        printf("Usage: bg %%<job number>\n");
        return 1;
    }
    
    int job_id = parse_job_number(T.argv[1]);
    if (job_id < 0) {
        printf("pssh: invalid job number: %s\n", T.argv[1]);
        return 1;
    }
    
    Job* job = find_job_by_job_id(job_id);
    if (!job) {
        printf("pssh: invalid job number: %s\n", T.argv[1]);
        return 1;
    }
    
    put_job_in_background(job, 1);
    return 0;
}

int builtin_kill(Task T) {
    if (!T.argv[1]) {
        printf("Usage: kill [-s <signal>] <pid> | %%<job> ...\n");
        return 1;
    }
    
    int sig = SIGTERM;  // Default signal
    int arg_start = 1;
    
    // Check for -s option
    if (!strcmp(T.argv[1], "-s") && T.argv[2]) {
        sig = atoi(T.argv[2]);
        arg_start = 3;
    }
    
    // Process each argument
    for (int i = arg_start; T.argv[i]; i++) {
        if (T.argv[i][0] == '%') {
            // Job number
            int job_id = parse_job_number(T.argv[i]);
            if (job_id < 0) {
                printf("pssh: invalid job number: %s\n", T.argv[i]);
                continue;
            }
            
            Job* job = find_job_by_job_id(job_id);
            if (!job) {
                printf("pssh: invalid job number: %s\n", T.argv[i]);
                continue;
            }
            
            if (killpg(job->pgid, sig) < 0) {
                perror("kill");
            }
        } else {
            // PID
            char* endptr;
            pid_t pid = strtol(T.argv[i], &endptr, 10);
            if (*endptr != '\0') {
                printf("pssh: invalid pid: %s\n", T.argv[i]);
                continue;
            }
            
            if (kill(pid, sig) < 0) {
                perror("kill");
            }
        }
    }
    
    return 0;
}

/*
 * builtin_which - implements the built-in which command.
 */
int builtin_which(Task T)
{
    if (!T.argv[1]) {
         fprintf(stdout, "usage: which command\n");
         return 1;
    }
    char *prog = T.argv[1];

    if (strchr(prog, '/') != NULL) {
         if (access(prog, X_OK) == 0)
              printf("%s\n", prog);
         return 0;
    }

    for (int i = 0; builtin[i] != NULL; i++) {
         if (!strcmp(prog, builtin[i])) {
              printf("%s: shell built-in command\n", prog);
              return 0;
         }
    }

    char *path_env = getenv("PATH");
    if (!path_env)
         return 1;
    char *path_dup = strdup(path_env);
    char *dir, *saveptr = NULL;
    char fullpath[PATH_MAX];
    for (dir = strtok_r(path_dup, ":", &saveptr); dir != NULL;
         dir = strtok_r(NULL, ":", &saveptr)) {
         snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, prog);
         if (access(fullpath, X_OK) == 0) {
              printf("%s\n", fullpath);
              free(path_dup);
              return 0;
         }
    }
    free(path_dup);
    return 0;
}