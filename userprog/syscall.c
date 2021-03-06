#include "devices/shutdown.h"//for shutdown_power_off
#include "threads/synch.h" //for lock
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <syscall-nr.h>
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <stdlib.h>
//#include <unistd.h>
//#include <sys/types.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "process.h"
#include <list.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
/* I put this in the .h file but idk if it's actually what we're supposed to do
typedef int pid_t;
*/
static struct fd_entry* get_fd_entry_by_fd(int fd);
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
      if(is_invalid(f->esp+4)) //force_exit();
      {
        force_exit();
        //f->eax=-1;
        //return; 
      }
  		char* name=*(( int*)(f->esp+4));
  		
      //printf("opening %s\n", name);

      //if(strcmp(name, ".")==0||strcmp(name, "..")==0)
      //{
      //  f->eax=-1;
      //  return;
      //}


  		lock_acquire(&fslock);
  		struct file* file_ptr = filesys_open(name);
  		lock_release(&fslock);

  		int fd=-1;

  		
  		if (file_ptr != NULL) {
        struct thread* current_thread = thread_current();
			
			  //now add new file descriptor entry into file descriptor table.
        struct fd_entry* fd_ptr = malloc(sizeof(struct fd_entry));

        struct inode* inode= file_get_inode(file_ptr);

        if(is_inode_dir(inode))
        {
          fd_ptr->file_ptr=NULL;
          struct dir* dir = dir_open(inode_reopen(inode));
          fd_ptr->dir_ptr = dir;
          file_close(file_ptr); 
        }
        else
        {
          fd_ptr->file_ptr=file_ptr;
          fd_ptr->dir_ptr=NULL;
        }
        fd_ptr->fd=current_thread->fd_id_counter;
        fd=fd_ptr->fd;

			  list_push_back(&current_thread->fd_table, &fd_ptr->elem);
			  //lock_acquire(&fslock);
			  current_thread->fd_id_counter++; //make sure every new file get a new id;
			  //lock_release(&fslock);

		  }
      else
      {
        //printf("shit, in syscall_open the fd pointer is NULL\n");
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
  			//printf("write -1\n");
        //force_exit();
        f->eax=-1;
        return;
  		}

  		int fd=*(( int*)(f->esp+4));
  		char* buffer=*(( int*)(f->esp+8));
  		int size=*(( int*)(f->esp+12));

      //printf("writing to fd %d\n",fd );

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
				int byte_written=file_write (fd_ptr->file_ptr, buffer, size);
				lock_release(&fslock);

        f->eax=byte_written;

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
      
      //lazy way to simulate that disk is full
      if(thread_current()->fd_id_counter >800)
      {
        f->eax =false;
        return;
      }
      
      if(is_invalid(f->esp+4))
      {
        //printf("invalid!\n");
       force_exit();
      }
  	 	char* name=*(( int*)(f->esp+4));
      	int size=*(( int*)(f->esp+8));

        //printf("create a file called %s\n", name);


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
      //printf("remove %s\n", name);


      lock_acquire(&fslock);
      bool result = filesys_remove(name);
      lock_release(&fslock);

      f->eax=result;
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
        file_seek(fd_ptr->file_ptr, position);
      }
      break;
  	}
  	case(SYS_TELL):{
  		//TODO: actually do this case
      int fd=*(( int*)(f->esp+4));
      struct list_elem* e = elem_with_fd(fd);
      if(e == NULL) {
        force_exit();
      } else {
        struct fd_entry *fd_ptr = list_entry(e, struct fd_entry, elem);
        f->eax = file_tell(fd_ptr->file_ptr);
      }
  		break;
  	}
  	case(SYS_CLOSE):{
  		int fd=*(( int*)(f->esp+4));
  		int size=*(( int*)(f->esp+8));
  		if(fd<0)
  			force_exit();

      //printf("closing fd %d\n", fd);


  		struct thread* current_thread = thread_current();
		struct list_elem* e = elem_with_fd(fd);
		if(e == NULL) 
			return false; // return false if fd not found
		struct list_elem*  return_e = list_remove (e);

		struct  fd_entry *fd_ptr = list_entry (e, struct fd_entry, elem);

		lock_acquire(&fslock);

    if(fd_ptr->file_ptr==NULL)
      dir_close(fd_ptr->dir_ptr);
    else
		  file_close(fd_ptr->file_ptr);

    list_remove(&fd_ptr->elem);

		lock_release(&fslock);

		free(fd_ptr); // Free the element we just removed, please also see open()
  		f->eax=fd;
  		break;
  	}
    //bool chdir (const char *dir)
    case(SYS_CHDIR):
    {
      char* name=*(( int*)(f->esp+4));

      //printf("change directory to %s\n", name);

      bool result;
      lock_acquire(&fslock);
      result = filesys_chdir(name);
      lock_release(&fslock);
      f->eax=result;
      break;

    }
    //bool mkdir (const char *dir)
    case(SYS_MKDIR):{
      char* name=*(( int*)(f->esp+4));

      //printf("mkdir!! make a directory call %s\n", name);


      bool result;
      lock_acquire(&fslock);
      result = filesys_mkdir(name);
      lock_release(&fslock);
      f->eax=result;
      break;
    }
    //bool readdir (int fd, char *name)
    case(SYS_READDIR):{
      //printf("readdir\n");
      int fd=*(( int*)(f->esp+4));
      char* name=*(( int*)(f->esp+8));
      if(fd < 0){
      	force_exit();
      }

      struct thread* current_thread = thread_current();
      struct list_elem* e = elem_with_fd(fd);
      if(e == NULL){
      	force_exit();
      }

      struct fd_entry *fd_ptr = list_entry(e, struct fd_entry, elem);
      struct inode *inode = dir_get_inode(fd_ptr->dir_ptr);

      if(!is_inode_dir(inode)){
        //printf("oh no!!! readdir failed!\n");
      	force_exit();
      } 

      bool result= dir_readdir((fd_ptr->dir_ptr), name);
      //if(result==true)
      //  printf("readdir is good!\n");
      //else
      //  printf("ohno!\n");

      f->eax = result;

      break;
    }
    //bool isdir (int fd)
    case(SYS_ISDIR):{
      printf("isdir!\n");
      bool result = false;
      int fd=*(( int*)(f->esp+4));
  	  		
      struct fd_entry* fde = get_fd_entry_by_fd(fd);
	  
	    result=(fde->file_ptr==NULL);

  	  f->eax=result;
      //printf("isdir done!\n");
      break;
    }
    //int inumber (int fd)
    case(SYS_INUMBER):{
      //sprintf("inumber! \n");
      int fd=*(( int*)(f->esp+4));

      lock_acquire(&fslock);

      struct fd_entry* fde=get_fd_entry_by_fd(fd);

      int inumber;
      if(fde->file_ptr==NULL)
        inumber=inode_get_inumber(dir_get_inode(fde->dir_ptr));
      else
        inumber=inode_get_inumber(file_get_inode(fde->file_ptr));

      lock_release(&fslock);

      f->eax=inumber;
      break;
    }

  }

  //thread_exit ();
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


static struct fd_entry* get_fd_entry_by_fd(int fd) {
  struct list_elem* e= elem_with_fd(fd);
  return list_entry(e, struct fd_entry, elem);

}
