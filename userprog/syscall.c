#include "devices/shutdown.h"//for shutdown_power_off
#include "threads/synch.h" //for lock
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <syscall-nr.h>
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
//#include <unistd.h>
//#include <sys/types.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "process.h"
#include <list.h>
/* I put this in the .h file but idk if it's actually what we're supposed to do
typedef int pid_t;
*/

static void syscall_handler (struct intr_frame *);
void force_exit();
static bool is_invalid(void* stack_ptr);
static bool is_invalid_addr(int addr);
static int get_user (const uint8_t *uaddr);
static struct list_elem* elem_with_fd(int fd);
struct lock fslock;


void
syscall_init (void)
{
  //LOCK FOR FILESYSTEM
  
  lock_init(&fslock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
	
	if( !is_user_vaddr( f->esp)  )//check if the esp is valid or if the stack is overflow   
		force_exit();


	uint32_t* stack_ptr =  (uint32_t*)(f->esp+0);

	// Check for valid arg pointers
  	if (!( is_user_vaddr (stack_ptr + 1) && is_user_vaddr (stack_ptr + 2) && is_user_vaddr (stack_ptr + 3)))
    	force_exit();

   int syscall_type = * (( int*)(f->esp));

  
  switch(  syscall_type )
  {
  	case(SYS_HALT):
  	{
  		shutdown_power_off();
  		break;

  	}  
  	

  	case(SYS_EXIT):
  	{
  	    //printf("EXIT!\n");
  	    struct thread *current_thread = thread_current();	
        int status=*(( int*)(f->esp+4));
        //printf("status %d\n", status);
        char* string=current_thread->name;
        char* name[15];
        char* nameptr=&name[0];
        //not sure why, but the name of the current thread is not correct, it sometimes includes arguement itself as well, so we need to single the command itself out.
        while(*string!=0 && *string!=' ')
        {
        	*nameptr=*string;
        	string++;
        	nameptr++;
        }

        *nameptr=0;
        current_thread->pcb_ptr->exit_status=status;
        printf("%s: exit(%d)\n",name, status);
        //printf("Hello\n");
  		thread_exit(); 
  		break;

  	}  
  	

  	case(SYS_EXEC):
  	{
  		if(is_invalid(f->esp+4)) force_exit();
  		char* cmd_line=*(( int*)(f->esp+4));

  		int tid;
  		lock_acquire(&fslock);
  		tid = process_execute (cmd_line);
  		lock_release(&fslock);
  		f->eax=tid;
  		break;

  	}  
  	

  	case(SYS_OPEN): 
  	{
  		if(is_invalid(f->esp+4)) force_exit();
  		char* name=*(( int*)(f->esp+4));
  		
  		lock_acquire(&fslock);
  		struct file* file_ptr = filesys_open(name);
  		lock_release(&fslock);

  		int fd=-1;

  		
  		if (file_ptr != NULL) {

  			struct thread* current_thread = thread_current();
			
			//now add new file descriptor entry into file descriptor table.
			struct fd_entry* fd_ptr = malloc(sizeof(struct fd_entry));
			fd_ptr->fd=current_thread->fd_id_counter;;
			fd=fd_ptr->fd;
			fd_ptr->file_ptr=file_ptr;
			list_push_back(&current_thread->fd_table, &fd_ptr->elem);
			//lock_acquire(&fslock);
			current_thread->fd_id_counter++; //make sure every new file get a new id;
			//lock_release(&fslock);
		}
		
  		
  		f->eax=fd;
    	break;
  	}  
  	/*int write (int fd, const void *buffer, unsigned size)
  	Writes size bytes from buffer to the open file fd. Returns the number of bytes actually written, which may be less than size if some bytes could not be written.
	Writing past end-of-file would normally extend the file, but file growth is not implemented by the basic file system. The expected behavior is to write as many bytes as possible up to end-of-file and return the actual number written, or 0 if no bytes could be written at all.

	Fd 1 writes to the console. Your code to write to the console should write all of buffer in one call to putbuf(), at least as long as size is not bigger than a few hundred bytes. (It is reasonable to break up larger buffers.) Otherwise, lines of text output by different processes may end up interleaved on the console, confusing both human readers and our grading scripts.
  	*/
  	case(SYS_WRITE): 
  	{
  		if(is_invalid(f->esp+8))
  		{
  			force_exit();
  		}

  		int fd=*(( int*)(f->esp+4));
  		char* buffer=*(( int*)(f->esp+8));
  		int size=*(( int*)(f->esp+12));



  		//printf("type: %d, fd: %d, buffer %d, size %d\n",type, fd, buffer, size );


  		if(fd == 1) 
  		{
    		putbuf(buffer, size);

 		}
 		else
 		{
 			
 			struct list_elem* e =elem_with_fd(fd);
			if(e == NULL)
			{
				//printf("not found\n");
				f->eax=-1;
			}
			else
			{
				//printf("found!\n");
				struct  fd_entry *fd_ptr = list_entry (e, struct fd_entry, elem);
				lock_acquire(&fslock);
				f->eax=file_write (fd_ptr->file_ptr, buffer, size);
				lock_release(&fslock);



			}
			break;
 		}
  	}
  	


  	case(SYS_WAIT):
  	{
  		int pid=*(( int*)(f->esp+4));
  		f->eax=process_wait(pid);
  		break;

  	}  

  	case(SYS_CREATE):{

  		if(is_invalid(f->esp+4)) force_exit();
  	 	char* name=*(( int*)(f->esp+4));
      	int size=*(( int*)(f->esp+8));
      	bool created = false;
  		
      	lock_acquire(&fslock);
      	created = filesys_create(name, size);
      	lock_release(&fslock);

      	f->eax = created;
  		break;
  	  
      
  	}
  	case(SYS_REMOVE):{
  		if(is_invalid(f->esp+4)) force_exit();
  		char* name=*(( int*)(f->esp+4));
  		break;
  	}
  	case(SYS_FILESIZE):{
  		int fd=*(( int*)(f->esp+4));
  		struct thread* current_thread=thread_current();
  		struct list_elem* e =elem_with_fd(fd);
  		if(e == NULL) return -1;//file does not exist
  		struct  fd_entry *fd_ptr = list_entry (e, struct fd_entry, elem);
  		f->eax=file_length (fd_ptr -> file_ptr) ;
  		break;
  	}
  	case(SYS_READ):{
  		if(is_invalid(f->esp+8)) force_exit();
  		int fd=*(( int*)(f->esp+4));
  		char* buffer=*(( int*)(f->esp+8));
  		int size=*(( int*)(f->esp+12));
  		//printf("fd %d\n", fd);
  		if(fd==0)//stdin
  		{
  			for(int i = 0; i < size; i++)
  			{
  				buffer[i] = input_getc();
  			}
			f->eax=size;
  		}
  		else
  		{
  			struct list_elem* e = elem_with_fd(fd);
  			if(e == NULL)
  			{
  				force_exit();
  			}
  			else
  			{
  				struct fd_entry *fd_ptr = list_entry(e, struct fd_entry, elem);
  				lock_acquire(&fslock);
  				f->eax=file_read (fd_ptr->file_ptr, buffer, size);
  				lock_release(&fslock);
  			}
  			
  		}
  		break;
  	}
  	case(SYS_SEEK):{
		if(is_invalid(f->esp+8)) force_exit();
  		int fd=*(( int*)(f->esp+4));
  		int position=*(( int*)(f->esp+8));
  		struct list_elem* e = elem_with_fd(fd);
  		if(e == NULL)
  		{
  			force_exit();
  		}
  		else
  		{
  			struct fd_entry *fd_ptr = list_entry(e, struct fd_entry, elem);
  			//Set the next place to be read to the beginning plus the bytes given
  			fd_ptr->file_ptr = (((char*)fd_ptr->file_ptr) + position);

  		}
  		break;
  	}
  	case(SYS_TELL):{
  		//TODO: actually do this case
  		break;
  	}
  	case(SYS_CLOSE):{
  		int fd=*(( int*)(f->esp+4));
  		int size=*(( int*)(f->esp+8));
  		if(fd<0)
  			force_exit;

  		struct thread* current_thread = thread_current();
		struct list_elem* e = elem_with_fd(fd);
		if(e == NULL) 
			return false; // return false if fd not found
		struct list_elem*  return_e = list_remove (e);

		struct  fd_entry *fd_ptr = list_entry (e, struct fd_entry, elem);
		lock_acquire(&fslock);
		file_close(fd_ptr->file_ptr);
		lock_release(&fslock);
		free(fd_ptr); // Free the element we just removed, please also see open()
  		f->eax=fd;
  		break;
  	}
  }

  //thread_exit ();
}


/*
* Terminates Pintos by calling shutdown_power_off() 
* (declared in "threads/init.h"). This should be seldom used, 
* because you lose some information about possible deadlock 
* situations, etc. 
*/
void halt (void){
	//TODO: implement this

}

/*
* Terminates the current user program, returning status to 
* the kernel. If the process's parent waits for it (see below), 
* this is the status that will be returned. Conventionally, 
* a status of 0 indicates success and nonzero values indicate 
* errors. 
*/
void exit (int status){
	//TODO: implement this

}

/*
* Runs the executable whose name is given in cmd_line, 
* passing any given arguments, and returns the new process's 
* program id (pid). Must return pid -1, which otherwise 
* should not be a valid pid, if the program cannot load or 
* run for any reason. Thus, the parent process cannot return 
* from the exec until it knows whether the child process 
* successfully loaded its executable. You must use 
* appropriate synchronization to ensure this. 
*/
pid_t exec (const char *cmd_line){
	//TODO: implement this
	return -1;
}
 
/*
*    Waits for a child process pid and retrieves the child's 
	exit status.

    If pid is still alive, waits until it terminates. Then, returns 
    the status that pid passed to exit. If pid did not call exit(), 
    but was terminated by the kernel (e.g. killed due to an exception), 
    wait(pid) must return -1. It is perfectly legal for a parent process 
    to wait for child processes that have already terminated by the time 
    the parent calls wait, but the kernel must still allow the parent to 
    retrieve its child's exit status, or learn that the child was 
    terminated by the kernel.

    wait must fail and return -1 immediately if any of the following 
    conditions is true:

        pid does not refer to a direct child of the calling process. 
        pid is a direct child of the calling process if and only if the 
        calling process received pid as a return value from a successful 
        call to exec.

        Note that children are not inherited: if A spawns child B and 
        B spawns child process C, then A cannot wait for C, even if B 
        is dead. A call to wait(C) by process A must fail. Similarly, 
        orphaned processes are not assigned to a new parent if their 
        parent process exits before they do.

        The process that calls wait has already called wait on pid. 
        That is, a process may wait for any given child at most once. 

    Processes may spawn any number of children, wait for them in any 
    order, and may even exit without having waited for some or all of 
    their children. Your design should consider all the ways in which 
    waits can occur. All of a process's resources, including its struct 
    thread, must be freed whether its parent ever waits for it or not, 
    and regardless of whether the child exits before or after its parent.

    You must ensure that Pintos does not terminate until the initial 
    process exits. The supplied Pintos code tries to do this by calling 
    process_wait() (in "userprog/process.c") from main() 
    (in "threads/init.c"). We suggest that you implement process_wait() 
    according to the comment at the top of the function and then 
    implement the wait system call in terms of process_wait().

    Implementing this system call requires considerably more work than 
    any of the rest.
*/
int wait (pid_t pid){
	//TODO: implement this
	return 0;
}

/*
* Creates a new file called file initially initial_size bytes in size. 
* Returns true if successful, false otherwise. Creating a new file does 
* not open it: opening the new file is a separate operation which would 
* require a open system call. 
*/
bool create (const char *file, unsigned initial_size){
	//TODO: implement this
	return false;
}

/*
* Deletes the file called file. Returns true if successful, false 
* otherwise. A file may be removed regardless of whether it is open or 
* closed, and removing an open file does not close it. See Removing an 
* Open File, for details. 
*/
bool remove (const char *file){
	//TODO: implement this
	return false;
}

/*
*    Opens the file called file. Returns a nonnegative integer handle 
called a "file descriptor" (fd), or -1 if the file could not be opened.

    File descriptors numbered 0 and 1 are reserved for the console: 
    fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is 
    standard output. The open system call will never return either of 
    these file descriptors, which are valid as system call arguments 
    only as explicitly described below.

    Each process has an independent set of file descriptors. File 
    descriptors are not inherited by child processes.

    When a single file is opened more than once, whether by a single 
    process or different processes, each open returns a new file 
    descriptor. Different file descriptors for a single file are \
    closed independently in separate calls to close and they do not 
    share a file position.
*/
int open (const char *file){
	//TODO: implement this
	return 0;
}

/*
* Returns the size, in bytes, of the file open as fd. 
*/
int filesize (int fd){
	//TODO: implement this
	return 0;
}

/*
* Reads size bytes from the file open as fd into buffer. Returns the 
* number of bytes actually read (0 at end of file), or -1 if the file 
* could not be read (due to a condition other than end of file). Fd 0 
* reads from the keyboard using input_getc(). 
*/
int read (int fd, void *buffer, unsigned size){
	//TODO: implement this
	return 0;
}

/*
*   Writes size bytes from buffer to the open file fd. Returns the 
	number of bytes actually written, which may be less than size if 
	some bytes could not be written.

    Writing past end-of-file would normally extend the file, but file 
    growth is not implemented by the basic file system. The expected 
    behavior is to write as many bytes as possible up to end-of-file 
    and return the actual number written, or 0 if no bytes could be 
    written at all.

    Fd 1 writes to the console. Your code to write to the console 
    should write all of buffer in one call to putbuf(), at least as 
    long as size is not bigger than a few hundred bytes. (It is 
    reasonable to break up larger buffers.) Otherwise, lines of text 
    output by different processes may end up interleaved on the 
    console, confusing both human readers and our grading scripts.
*/
int write (int fd, const void *buffer, unsigned size){
	//TODO: implement this
	return 0;
}

/*
*   Changes the next byte to be read or written in open file fd to 
	position, expressed in bytes from the beginning of the file. 
	(Thus, a position of 0 is the file's start.)

    A seek past the current end of a file is not an error. A later 
    read obtains 0 bytes, indicating end of file. A later write extends 
    the file, filling any unwritten gap with zeros. (However, in Pintos 
    files have a fixed length until project 4 is complete, so writes 
    past end of file will return an error.) These semantics are 
    implemented in the file system and do not require any special 
    effort in system call implementation.
*/
void seek (int fd, unsigned position){
	//TODO: implement this

}

/*
* Returns the position of the next byte to be read or written in open 
* file fd, expressed in bytes from the beginning of the file. 
*/
unsigned tell (int fd){
	//TODO: implement this
	return 0;
}

/*
* Closes file descriptor fd. Exiting or terminating a process 
* implicitly closes all its open file descriptors, as if by calling 
* this function for each one. 
*/
void close (int fd){
	//TODO: implement this

  thread_exit ();
}



/*
	check if the given stack ptr points to a invalid user address
*/
static bool is_invalid(void* stack_ptr)
{

	struct thread *current_thread = thread_current();
	if( *(  (uint32_t*)stack_ptr  )  > PHYS_BASE  ||  pagedir_get_page(current_thread->pagedir, *((int*)stack_ptr)  )  ==NULL ) // illegal kernal access or page fault access
	{
        
		return true;
  		

	}
	else
		return false;
}


static bool is_invalid_addr(int addr)
{

	struct thread *current_thread = thread_current();
	if( addr > PHYS_BASE  ||  pagedir_get_page(current_thread->pagedir, addr  )  ==NULL ) // illegal kernal access or page fault access
	{
        
		return true;

	}
	else
		return false;
}

/*
	mannually exit with -1
*/
void force_exit()
{
	 struct thread *current_thread = thread_current();
	 int status=-1;
     char* string=current_thread->name;
     char* name[100];
     char* nameptr=&name[0];
     //not sure why, but the name of the current thread is not correct, it sometimes includes arguement itself as well, so we need to single the command itself out.
     while(*string!=0 && *string!=' ')
     {
       	*nameptr=*string;
        string++;
        nameptr++;
     }

     *nameptr=0;

     printf("%s: exit(%d)\n",name, status);
    
  	 thread_exit(); 
}
//search and return a list_elem ptr with fd specified
static struct list_elem* elem_with_fd(int fd){
	//printf("called!!!\n");
	struct thread* current_thread=thread_current();
  	struct list_elem* e;
  	for (e = list_begin (&current_thread->fd_table); e != list_end(&current_thread->fd_table); e = list_next (e))
  	{
  		struct  fd_entry *fd_ptr = list_entry (e, struct fd_entry, elem);
  		if(fd_ptr->fd==fd)
  		{
  			//printf("Found!\n");
  			return e;
  		}
  	}
  	return NULL;
}

static int get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}