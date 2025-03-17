#ifndef JOB_CONTROL_H
#define JOB_CONTROL_H

#include <sys/types.h>

typedef enum {
    STOPPED,
    TERM,
    BG,
    FG,
} JobStatus;

typedef struct {
    char* name;           
    pid_t* pids;         
    unsigned int npids;   
    pid_t pgid;          
    JobStatus status;     
    int job_id;          
} Job;

extern Job jobs[100];
extern int num_jobs;

// Helper function for terminal control
void set_fg_pgid(pid_t pgid);

// Job control functions
void init_job_control(void);
int add_job(pid_t* pids, int npids, pid_t pgid, char* cmdline, JobStatus status);
void remove_job(int job_id);
void update_job_status(int job_id, JobStatus status);
Job* find_job_by_pgid(pid_t pgid);
Job* find_job_by_job_id(int job_id);
void print_job_status(Job* job, int show_pid);
void mark_process_status(pid_t pid, int status);
void wait_for_job(Job* job);
void put_job_in_foreground(Job* job, int cont);
void put_job_in_background(Job* job, int cont);
void continue_job(Job* job, int foreground);

// Helper functions
int job_is_completed(Job* job);
int job_is_stopped(Job* job);
void cleanup_completed_jobs();
int process_exists(pid_t pid);

#endif