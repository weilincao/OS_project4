#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"//for sema
#include "filesys/file.h"
#include <stdlib.h>


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);




struct fd_entry
{
	int fd;
	struct list_elem elem;
	struct file* file_ptr;

};

struct pcb_t
{
	uint32_t pid;
	int status;
	struct list_elem elem;
	struct semaphore wait_sema;
	struct semaphore load_sema;
	int exit_status;
	
};


#endif /* userprog/process.h */
