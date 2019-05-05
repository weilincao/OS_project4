#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();

  struct thread* current_thread=thread_current();
  current_thread->current_working_dir=dir_open_root();
  printf("filesys initialized!\n");
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *input, off_t initial_size)
{
  if (strlen(input) == 0){
    printf("file name is empty!\n");
  }
  char path[strlen(input)+1];   
  char filename[strlen(input)+1];
  //extract_filename_and_path(input, filename, path);
  //struct dir* dir=get_dir_from_path(path);

  //printf("filenames is %s\n", filename);
  //if(*path =='\0')
  //  printf("path is empty!!\n");
  //else
  //  printf("path is %s\n", path );

  struct dir *dir = dir_open_root ();
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size,false)
                  && dir_add (dir, input, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_mkdir (const char *input)
{

  if (strlen(input) == 0){
    printf("mkdir input is nothing!\n");
  }
  char path[strlen(input)+1];   
  char filename[strlen(input)+1];

  extract_filename_and_path(input, filename, path);
  //printf("directory names is %s\n", filename);
  //if(*path =='\0')
  //  printf("path is empty!!\n");
  //else
  //  printf("path is %s\n", path );

  struct dir* dir=get_dir_from_path(path);

  //if(dir==NULL)
  //  printf("oh no!!!\n");
  //struct dir *dir = dir_open_root ();
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, dir)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  //if(success==true)
  //  printf("success!\n");
  //else
  //  printf("failed!\n");
  return success;
}

bool
filesys_chdir(const char* path)
{
  struct dir* dir=get_dir_from_path(path);
  if(dir==NULL)
    return false;

  thread_current()->current_working_dir=dir;
  return true;

} 


/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *input)
{
  //printf("filesys_open!\n");
  //char* path=malloc( (strlen(input)+1) * sizeof(char));
  //char* filename=malloc( (strlen(input)+1) * sizeof(char));
  //test_extract_filename_and_path(input,path,filename);


  //struct dir* dir=get_dir_from_path(path);

  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;
  
  if (dir != NULL)
    dir_lookup (dir, input, &inode); // change name to filename 
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, NULL))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
