/* job_control.c
* handles all things job related + fg + bg + signals etc etc
*
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <errno.h>

#include "job_control.h"

Job jobs[100];
int num_jobs = 0;

/**
 * Sets the process group ID that has control of the terminal foreground
 * Temporarily ignores SIGTTOU to prevent the shell from being suspended 
 * when it gives terminal control to another process group
 */
void set_fg_pgid(pid_t pgid) {
    void (*old_handler)(int) = signal(SIGTTOU, SIG_IGN);
    
    tcsetpgrp(STDIN_FILENO, pgid);
    
    signal(SIGTTOU, old_handler);
}

/**
 * Signal handler for SIGUSR1
 * Used as a synchronization mechanism when switching foreground jobs
 */
static void sigusr1_handler(int sig) {
    (void)sig;  
}

/**
 * Signal handler for SIGCHLD
 * Handles child process status changes (termination, suspension, continuation)
 * Updates job status accordingly and manages job list cleanup
 */
static void sigchld_handler(int sig) {
    (void)sig; 
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job* job = NULL;
        for (int i = 0; i < num_jobs; i++) {
            for (unsigned int j = 0; j < jobs[i].npids; j++) {
                if (jobs[i].pids[j] == pid) {
                    job = &jobs[i];
                    break;
                }
            }
            if (job) break;
        }
        
        if (!job) continue; 
        
        if (WIFSTOPPED(status)) {
            if (job->status == FG) {
                job->status = STOPPED;
                
                set_fg_pgid(getpid());
                
                // status message
                printf("\n[%d] + suspended %s\n", job->job_id, job->name);
                fflush(stdout);
                
                kill(getpid(), SIGUSR1);
            }
        } else if (WIFCONTINUED(status)) {

            if (job->status == STOPPED) {
                job->status = BG;
                printf("[%d] + continued %s\n", job->job_id, job->name);
                fflush(stdout);
            }
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            
            for (unsigned int j = 0; j < job->npids; j++) {
                if (job->pids[j] == pid) {
                    job->pids[j] = 0;  // terminated
                    break;
                }
            }
            
            int all_done = 1;
            for (unsigned int j = 0; j < job->npids; j++) {
                if (job->pids[j] != 0) {
                    all_done = 0;
                    break;
                }
            }
            
            if (all_done) {
                if (job->status == FG) {
                    set_fg_pgid(getpid());
                    remove_job(job->job_id);
                    
                    kill(getpid(), SIGUSR1);
                } else if (job->status == BG) {
                    printf("[%d] + done %s\n", job->job_id, job->name);
                    fflush(stdout);
                    remove_job(job->job_id);
                }
            }
        }
    }
}

/**
 * Signal handler for SIGTSTP (Ctrl-Z)
 * Sends SIGTSTP to the foreground job if one exists
 */
static void sigtstp_handler(int sig) {
    (void)sig;  
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].status == FG) {
            killpg(jobs[i].pgid, SIGTSTP);
            return;
        }
    }
    
    write(STDOUT_FILENO, "\n", 1);
    kill(getpid(), SIGUSR1);
}

/**
 * Signal handler for SIGINT (Ctrl-C)
 * Sends SIGINT to the foreground job if one exists
 */
static void sigint_handler(int sig) {
    (void)sig;  
    
    int found_fg_job = 0;
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].status == FG) {
            killpg(jobs[i].pgid, SIGINT);
            found_fg_job = 1;
            break;
        }
    }
    
    if (!found_fg_job) {
        write(STDOUT_FILENO, "\n", 1);
        kill(getpid(), SIGUSR1);
    }
}

/**
 * Initialize job control subsystem
 * Sets up signal handlers and process group for the shell
 */
void init_job_control(void) {
    pid_t shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        perror("setpgid");
        exit(EXIT_FAILURE);
    }

    set_fg_pgid(shell_pgid);

    struct sigaction sa;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = sigtstp_handler;
    sigaction(SIGTSTP, &sa, NULL);

    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    
    sa.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &sa, NULL);

    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
}

/**
 * Check if a process with the given pid exists
 * Returns 1 if process exists, 0 otherwise
 */
int process_exists(pid_t pid) {
    if (pid <= 0) return 0;
    return kill(pid, 0) == 0 || errno != ESRCH;
}

/**
 * Check if a job is completed (all processes have terminated)
 * Returns 1 if completed, 0 otherwise
 */
int job_is_completed(Job* job) {
    if (!job) return 1;
    
    for (unsigned int i = 0; i < job->npids; i++) {
        if (job->pids[i] != 0 && process_exists(job->pids[i])) {
            return 0;  
        }
    }
    return 1;  
}

/**
 * Check if a job is stopped (suspended)
 * Returns 1 if stopped, 0 otherwise
 */
int job_is_stopped(Job* job) {
    if (!job) return 0;
    
    if (job_is_completed(job)) return 0;
    
    return job->status == STOPPED;
}

/**
 * Update the status of a specific process in a job
 * (Function stub - implementation not provided in the code)
 */
void mark_process_status(pid_t pid, int status) {
    (void)pid;
    (void)status;
}

/**
 * Print the status of a job
 */
void print_job_status(Job* job, int show_pid) {
    if (!job) return;

    printf("[%d] + ", job->job_id);
    
    switch (job->status) {
        case STOPPED:
            printf("stopped ");
            break;
        case BG:
            printf("running ");
            break;
        case FG:
            printf("continued ");
            break;
        case TERM:
            printf("done ");
            break;
    }

    printf("%s", job->name);
    
    if (show_pid) {
        for (unsigned int i = 0; i < job->npids; i++) {
            if (job->pids[i] > 0) {  
                printf(" %d", job->pids[i]);
            }
        }
    }
    printf("\n");
    fflush(stdout);
}

/**
 * Move a job to the foreground
 * If cont is true, continue the job if it was stopped
 */
void put_job_in_foreground(Job* job, int cont) {
    if (!job) return;
    
    set_fg_pgid(job->pgid);

    if (cont) {
        if (job->status == STOPPED) {
            job->status = FG;
            if (killpg(job->pgid, SIGCONT) < 0) {
                perror("killpg (SIGCONT)");
            }
        }
    }

    job->status = FG;
    
    wait_for_job(job);
}

/**
 * Move a job to the background
 * If cont is true, continue the job if it was stopped
 */
void put_job_in_background(Job* job, int cont) {
    if (!job) return;
    
    if (cont && job->status == STOPPED) {
        job->status = BG;
        if (killpg(job->pgid, SIGCONT) < 0) {
            perror("killpg (SIGCONT)");
        }
        printf("[%d] + continued %s\n", job->job_id, job->name);
        fflush(stdout);
    } else if (!cont) {
        job->status = BG;
        printf("[%d]", job->job_id);
        for (unsigned int i = 0; i < job->npids; i++) {
            if (job->pids[i] > 0) {  
                printf(" %d", job->pids[i]);
            }
        }
        printf("\n");
        fflush(stdout);
    }
}

/**
 * Wait for a foreground job to complete or be suspended
 * Returns terminal control to the shell when done
 */
void wait_for_job(Job* job) {
    if (!job) return;
    
    if (job_is_completed(job)) {
        job->status = TERM;
        set_fg_pgid(getpid());
        remove_job(job->job_id);
        return;
    }
    
    while (job->status == FG) {
        if (job_is_completed(job) || job_is_stopped(job)) {
            break;
        }
        
        pause();
    }
    
    set_fg_pgid(getpid());
}

/**
 * Add a new job to the job table
 * Returns the new job ID or -1 on error
 */
int add_job(pid_t* pids, int npids, pid_t pgid, char* cmdline, JobStatus status) {
    int job_id = 0;
    
    for (int i = 0; i < 100; i++) {
        int id_taken = 0;
        for (int j = 0; j < num_jobs; j++) {
            if (jobs[j].job_id == i) {
                id_taken = 1;
                break;
            }
        }
        if (!id_taken) {
            job_id = i;
            break;
        }
    }

    if (num_jobs >= 100) {
        fprintf(stderr, "Maximum number of jobs exceeded\n");
        return -1;
    }

    // initialize new job

    jobs[num_jobs].job_id = job_id;
    jobs[num_jobs].pgid = pgid;
    jobs[num_jobs].status = status;
    jobs[num_jobs].name = strdup(cmdline);
    jobs[num_jobs].npids = npids;
    jobs[num_jobs].pids = malloc(npids * sizeof(pid_t));
    memcpy(jobs[num_jobs].pids, pids, npids * sizeof(pid_t));

    num_jobs++;
    return job_id;
}
/**
 * Remove a job from the job table by job ID
 */
void remove_job(int job_id) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].job_id == job_id) {
            free(jobs[i].name);
            free(jobs[i].pids);
            
            for (int j = i; j < num_jobs - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            num_jobs--;
            break;
        }
    }
}

/**
 * Find and clean up any completed background jobs
 * Prints completion messages as needed
 */
void cleanup_completed_jobs() {
    for (int i = 0; i < num_jobs; i++) {
        if (job_is_completed(&jobs[i])) {
            if (jobs[i].status == BG) {
                printf("[%d] + done %s\n", jobs[i].job_id, jobs[i].name);
                fflush(stdout);
            }
            remove_job(jobs[i].job_id);
            i--;  
        }
    }
}

/**
 * Find a job by its process group ID
 * Returns pointer to job or NULL if not found
 */
Job* find_job_by_pgid(pid_t pgid) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].pgid == pgid) {
            return &jobs[i];
        }
    }
    return NULL;
}

/**
 * Find a job by its job ID
 * Returns pointer to job or NULL if not found
 */
Job* find_job_by_job_id(int job_id) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].job_id == job_id) {
            return &jobs[i];
        }
    }
    return NULL;
}

/**
 * Continue a stopped job
 * Puts job in foreground or background based on foreground parameter
 */
void continue_job(Job* job, int foreground) {
    if (!job) return;
    
    if (foreground)
        put_job_in_foreground(job, 1);
    else
        put_job_in_background(job, 1);
}