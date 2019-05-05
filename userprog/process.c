#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "syscall.h"

#define LOGGING_LEVEL 6

#include "lib/log.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  //printf("execute!!!!!!!!!!!!!!!!!!!!!!\n");
  char *fn_copy;
  tid_t tid;
  struct thread *current_thread = thread_current();
  // NOTE:
  // To see this print, make sure LOGGING_LEVEL in this file is <= L_TRACE (6)
  // AND LOGGING_ENABLE = 1 in lib/log.h
  // Also, probably won't pass with logging enabled.
  log(L_TRACE, "Started process execute: %s", file_name);
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
  else//the process is going to be execute successfully
  {
    
    struct thread* child=thread_with_tid(tid);
    if(child==NULL)
    {
      return -1;
    }
    //for file sys
    child->current_working_dir = dir_reopen(current_thread->current_working_dir); // same current working directory between parent and child!

    list_push_back(&current_thread->child_pcb_list, &child->pcb_ptr->elem);
    struct pcb_t * pcb_ptr= child->pcb_ptr; 
    sema_init(&pcb_ptr->wait_sema, 0);
    sema_down(&pcb_ptr->load_sema);
    if (pcb_ptr->exit_status == -1) return -1;//if the load failed(for example, no executable is found)
  }

  //palloc_free_page (file_name);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  log(L_TRACE, "start_process()");

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  //weilin added this because he wants to load before the execute
  struct thread* current_thread=thread_current();
  sema_up(&current_thread->pcb_ptr->load_sema);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success)
  {
    current_thread->pcb_ptr->exit_status = -1;
    force_exit();
  }
    
    //thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED)
{
  
  struct thread* current_thread = thread_current();
  struct list *child_pcb_list = &current_thread->child_pcb_list;
  struct list_elem *e;
  struct pcb_t *child;

  for (e = list_begin (child_pcb_list); e != list_end (child_pcb_list);e = list_next (e)) 
  {
    child = list_entry(e, struct pcb_t, elem);
    if (child->pid == child_tid)
    {
      //printf("found!\n");
      break; // found child
    } 
  }
  

  if (e == list_end(child_pcb_list)){
    //printf("not found!\n");
    return -1;
  } 
  
  sema_down(&child->wait_sema);
  int exit_status=child->exit_status;
  list_remove(e);
  return exit_status;
  }

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

    //weilin code:
    sema_up(&cur->pcb_ptr->wait_sema);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char* cmd_line);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *cmd_line, void (**eip) (void), void **esp)
{
  log(L_TRACE, "load()");
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  //THIS IS WHAT I CHANGED
  char getFileName[3000];
  char* ptr = cmd_line;
  strlcpy(getFileName, cmd_line, sizeof(getFileName));
  char** savePtr = &ptr;
  char* workingString = getFileName;
  char* file_name = strtok_r(workingString, " \n", savePtr);

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, cmd_line
    ))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  log(L_TRACE, "load_segment()");

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}




/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
//printf("command line: %s\n",cmd_line);
        /**esp = PHYS_BASE;
        char* ptr4= 0xbfffffff;
        *ptr4 = '\0';
        char* ptr3= 0xbffffffe;
        *ptr3 = 'e';
        char* ptr2= 0xbffffffd;
        *ptr2 = 'n';
        char* ptr1= 0xbffffffc;
        *ptr1 = 'o';

        int* argv1=0xbffffff8;
        *argv1=0;

        int* argv0=0xbffffff4;
        *argv0=0xbffffffc;

        int* argv=0xbffffff0;
        *argv=0xbffffff4;

        int* argc=0xbfffffec;
        *argc=1;

        int* ret=0xbfffffe8;
        *ret=0;

        *esp=ret;*/

        /*
        * Parse cmd_line into tokens using strtok
        * How many tokens? (=argc) (no more than 4 -- 3 arguments?)
        * Iterate through each token backwards & put them on the stack
        *     Does it include null terminators by itself? (probably not)
        *     Align the token, then start the next one above it?
        * Put a 0 (4B)
        * Put a pointer to wherever the last (first) letter of
        *   argv[0] is (4B)
        * Put a pointer to the pointer you just put (4B)
        * Put argc (4B)
        * Put a 0 (4B) and have the stack pointer point there
        */



static bool
setup_stack (void **esp, char* cmd_line)
{
  uint8_t *kpage;
  bool success = false;

  log(L_TRACE, "setup_stack()");

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success) {

        
        




        *esp = PHYS_BASE;
        char* workingCmdLine = cmd_line;

        char** argvArr = malloc( sizeof(char*) * 25); 
        int argc = 0;
        int argvIdx = 0;
        char* token = NULL;
        char** savePtr = &workingCmdLine;

        token = strtok_r(workingCmdLine, " \n", savePtr);

        while(token != NULL){
          argvArr[argvIdx] = token;
          argc++;
          argvIdx++;
          token = strtok_r(NULL, " \n", savePtr);
        }

        int alignCounter = 0;
        int alignZeros = 0;
        char* argv = NULL;
        char** argvPtrs = malloc( sizeof(char*) * 25);                       

        for(int i = 0; i < argc; i++){
          *esp = (char*)*esp - 1;
          *((char*)*esp) = '\0';
          alignCounter++;
          for(int j = (strlen(argvArr[i]) - 1); j >= 0; j--){
            *esp = (char*)*esp - 1;
            *((char*)*esp) = argvArr[i][j];
            alignCounter++;
          }
          argvPtrs[i] = (char*)*esp;
        }

        if((alignCounter % 4) != 0){
          alignZeros = 4 - (alignCounter % 4);
          for(int k = 0; k < alignZeros; k++){
            *esp = (char*)*esp - 1;
            *((char*)*esp) = NULL;
          }
        }



        *esp = (int*)*esp - 1;
        *((int*)*esp) = NULL;

        int* argvPtr = NULL;
        for(int x = argc - 1; x >= 0; x--){
          *esp = (int*)*esp -1;
          *((int*)*esp) = (int) argvPtrs[x];
          argvPtr = (int*)*esp;
        }

        *esp = (int*)*esp - 1;
        *((int*)*esp) = (int)argvPtr;

        *esp = (int*)*esp - 1;
        *((int*)*esp) = argc;

        *esp = (int*)*esp - 1;
        *((int*)*esp) = 0;
        //free(argvPtrs);
        //free(argvArr);

	  }
      else {
        palloc_free_page (kpage);
	  }
	  // hex_dump( *(int*)esp, *esp, 128, true ); // NOTE: uncomment this to check arg passing
    }
  return success;
}
/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
