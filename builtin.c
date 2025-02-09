#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "builtin.h"
#include "parse.h"

static char *builtin[] = {
    "exit",   /* exits the shell */
    "which",  /* displays full path to command */
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
        exit (EXIT_SUCCESS);
    } else if (!strcmp(T.cmd, "which")) {
        builtin_which(T);
    } else {
        printf("pssh: builtin command: %s (not implemented!)\n", T.cmd);
    }
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