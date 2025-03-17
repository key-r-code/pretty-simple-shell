#ifndef BUILTIN_H
#define BUILTIN_H

#include "parse.h"

int is_builtin(char *cmd);
void builtin_execute(Task T);
int builtin_which(Task T); 
int builtin_jobs(Task T);
int builtin_fg(Task T);
int builtin_bg(Task T);
int builtin_kill(Task T);

#endif