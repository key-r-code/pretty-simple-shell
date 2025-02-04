#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>

#include "builtin.h"
#include "parse.h"

/*******************************************
 * Set to 1 to view the command line parse *
 * Set to 0 before submitting!             *
 *******************************************/
#define DEBUG_PARSE 1


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
    return  "$ ";
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
    unsigned int t;

    for (t = 0; t < P->ntasks; t++) {
        if (is_builtin(P->tasks[t].cmd)) {
            builtin_execute(P->tasks[t]);
        }
        else if (command_found(P->tasks[t].cmd)) {
            printf("pssh: found but can't exec: %s\n", P->tasks[t].cmd);
        }
        else {
            printf("pssh: command not found: %s\n", P->tasks[t].cmd);
            break;
        }
    }
}


int main(int argc, char **argv)
{
    char *cmdline;
    Parse *P;

    print_banner();

    while (1) {
        /* do NOT replace readline() with scanf() or anything else! */
        cmdline = readline(build_prompt());
        if (!cmdline)       /* EOF (ex: ctrl-d) */
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
