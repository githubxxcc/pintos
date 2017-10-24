#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct cmd_frame 
{
    char * prog_name;                   /* Program name */
    int argv_len ;                      /* Length of the command line*/
    int argc;                           /* Argument counts */
    char * last_argv;                    /* Boundary addrss of the arguments */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
