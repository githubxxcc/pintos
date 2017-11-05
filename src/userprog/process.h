#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <list.h>

struct cmd_frame 
{
    char * prog_name;                   /* Program name */
    int argv_len ;                      /* Length of the command line*/
    int argc;                           /* Argument counts */
    void* aux;                          /* Aux data pointer */
    struct semaphore * loaded;          /* Semaphore indicated loaded */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
