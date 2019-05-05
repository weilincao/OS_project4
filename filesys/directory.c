#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, struct dir* parent_dir)
{
  //printf("creating directory!!\n");

  int parent_sector;
  if(parent_dir==NULL)//for root directory
    parent_sector=sector;
  else
    parent_sector=inode_get_inumber(parent_dir->inode);
  if(!inode_create (sector, 2* sizeof (struct dir_entry),true))//two for . and ..
  {
    printf("directory creation failed!\n");
    return false;
  }
  struct inode *inode = inode_open (sector); //add this into list of inode in RAM(runtime).
  struct dir * dir = dir_open (inode);

  dir_add (dir, ".", sector);
  dir_add (dir, "..", parent_sector);
  dir_close (dir);

  //printf("finish creating!\n");
  return true;

}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}



struct dir * get_dir_from_path (char* path){

  char* path_copy=malloc(strlen(path)+1); //because the strtok will modify the string and we probably don't want original path got modify, or do we?
  strlcpy(path_copy, path, strlen(path)+1);
  char* ptr=path_copy;
  struct dir* current_dir;

  if(path[0]=='/')
  {
    current_dir=dir_open_root();
    ptr++;
  }
  else
  {
    current_dir=dir_reopen(thread_current()->current_working_dir);
    //current_dir=dir_open_root();

  }



  char *token, *saveptr;
  token = strtok_r(ptr, "/", &saveptr);

  while (token != NULL) {

    struct inode *inode;

    if(! dir_lookup(current_dir, token, &inode)) {
      dir_close(current_dir);
      free(path_copy);
      return NULL; // such directory not exist
    }
    dir_close(current_dir);
    current_dir=dir_open(inode);
    token = strtok_r(NULL, "/", &saveptr);
  }
  free(path_copy);
  
  
  return current_dir;
}


void extract_filename_and_path(char* input, char* filename, char* path)
{
  //logic behind: the file name is the string between the /0 and the last /
  
  if(strlen(input)==0)
  {
    printf("invalid input: input string length is zero\n");
  }

  char* ptr= input+ strlen(input)-1; //the pointer points to the last charater
  int filename_length=1;

  while( ptr!=input  ) //if pointer is already the first letter of input, stop; if pointer +1 is /, stop as well && *(ptr-1) != '/'
  {
    filename_length++;
    ptr--;
  }
  //after the loop, the pointer should point to the first letter of file name
  //so now we can copy from the first letter to the last letter in the ptr to filename
  strlcpy(filename,ptr,filename_length+1);


  //the rest should be the path
  if((strlen(input)-filename_length)>0)
    strlcpy(path,input, (strlen(input)-filename_length)+1  );
  else
    *path='\0';

}
void test_extract_filename_and_path(char* input, char* filename, char* path)
{
  
}